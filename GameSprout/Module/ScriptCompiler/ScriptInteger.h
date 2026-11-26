#pragma once

#include <QString>
#include <bit>
#include <cstdint>
#include <limits>

// A reflected integer may be negative or use every bit of an unsigned value.
// Store its two's-complement bits so sign extension and arithmetic never need
// compiler-specific integer types or a floating-point round trip.
struct ScriptInteger {
    std::uint64_t Bits = 0;
    bool Negative = false;

    static ScriptInteger Signed(std::int64_t value) {
        return {static_cast<std::uint64_t>(value), value < 0};
    }

    static bool Parse(const QString& literal, ScriptInteger& result) {
        bool valid = false;
        if (literal.startsWith('-')) result = Signed(literal.toLongLong(&valid));
        else result = {literal.toULongLong(&valid), false};
        return valid;
    }

    std::int64_t ToSigned() const { return std::bit_cast<std::int64_t>(Bits); }
    bool FitsSigned() const { return Negative || Bits <= std::numeric_limits<std::int64_t>::max(); }
    std::uint64_t Magnitude() const { return Negative ? std::uint64_t{0} - Bits : Bits; }

    bool operator==(const ScriptInteger&) const = default;
    bool operator<(const ScriptInteger& other) const {
        return Negative != other.Negative ? Negative : Bits < other.Bits;
    }
};
