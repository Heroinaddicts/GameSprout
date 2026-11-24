#!/usr/bin/env python3
"""Bake world-space diffuse SH from six-face GameSprout captures into .irradiance."""
import argparse
from pathlib import Path
import struct

import numpy as np

from BakeEnvironment import diffuse_cube, read_cube


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture_directory", type=Path, help="contains numbered 0.gscube, 1.gscube… in X/Y/Z order")
    parser.add_argument("output", type=Path)
    parser.add_argument("--grid", type=int, nargs=3, metavar=("X", "Y", "Z"), required=True)
    args = parser.parse_args()
    if any(count < 1 or count > 32 for count in args.grid):
        parser.error("grid axes must contain 1 to 32 samples")
    if args.output.suffix != ".irradiance":
        parser.error("output must end in .irradiance")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(".irradiance.tmp")
    try:
        with temporary.open("wb") as output:
            output.write(struct.pack("<8s4I", b"GSIRR001", *args.grid, 0))
            for index in range(int(np.prod(args.grid))):
                cube = read_cube(args.capture_directory / f"{index}.gscube")
                output.write(diffuse_cube(cube).astype("<f4").tobytes())
        temporary.replace(args.output)
    finally:
        temporary.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
