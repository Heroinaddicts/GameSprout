#!/usr/bin/env python3
"""Write the packaged game files as a single, sequential mobile asset."""

import argparse
import hashlib
from pathlib import Path
import re
import struct


MAGIC = b"GSPACK1\n"
ARTIFACT_NAME = re.compile(r"[0-9a-fA-F]{32}_[0-9]+\.gsasset")


def write_package(source: Path, destination: Path) -> None:
    files = [source / "Player.json", source / "ProjectSettings.json"]
    for artifact in sorted((source / "Artifact").iterdir()):
        if not ARTIFACT_NAME.fullmatch(artifact.name):
            raise ValueError(f"Unexpected artifact name: {artifact.name}")
        files.append(artifact)

    digest = hashlib.sha256()
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("wb") as output:
        output.write(MAGIC)
        output.write(bytes(digest.digest_size * 2))

        def write(data: bytes) -> None:
            output.write(data)
            digest.update(data)

        write(struct.pack("<Q", len(files)))
        for path in files:
            if path.is_symlink() or not path.is_file():
                raise ValueError(f"Invalid payload file: {path}")
            name = path.relative_to(source).as_posix().encode("ascii")
            size = path.stat().st_size
            write(struct.pack("<QQ", len(name), size))
            write(name)
            with path.open("rb") as payload:
                remaining = size
                while remaining:
                    block = payload.read(min(remaining, 1024 * 1024))
                    if not block:
                        raise ValueError(f"Truncated payload: {path}")
                    write(block)
                    remaining -= len(block)
                if payload.read(1):
                    raise ValueError(f"Payload changed while packaging: {path}")
        output.seek(len(MAGIC))
        output.write(digest.hexdigest().encode("ascii"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("payload", type=Path)
    parser.add_argument("output", type=Path)
    arguments = parser.parse_args()
    write_package(arguments.payload, arguments.output)
