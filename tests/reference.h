// reference.h : line-for-line transliteration of the C# ArithmeticModule.
//
// This is the specification the DLL is tested against. It is deliberately
// *not* a state machine and keeps the C# semantics, including the masked
// shift count (`ulong >> n` in C# shifts by `n & 63`).
#pragma once

#include <bit>
#include <cstdint>

namespace reference {

constexpr int Bias = 127;

inline uint64_t AddBits(uint64_t a, uint64_t b)
{
    while (b != 0) {
        uint64_t carry = (a & b) << 1;
        a = a ^ b;
        b = carry;
    }
    return a;
}

inline uint64_t SubBits(uint64_t a, uint64_t b)
{
    return AddBits(a, AddBits(~b, 1));
}

inline uint64_t MulBits(uint64_t a, uint64_t b)
{
    uint64_t result = 0;
    while (b != 0) {
        if ((b & 1) == 1)
            result = AddBits(result, a);
        a <<= 1;
        b >>= 1;
    }
    return result;
}

inline uint64_t DivBits(uint64_t a, uint64_t b)
{
    uint64_t result = 0;
    uint64_t remainder = 0;
    for (int i = 63; i >= 0; i--) {
        remainder <<= 1;
        remainder |= (a >> i) & 1;
        if (remainder >= b) {
            remainder = SubBits(remainder, b);
            result |= 1ULL << i;
        }
    }
    return result;
}

inline int Exponent(int32_t bits)
{
    return (bits >> 23) & 0xFF;
}

inline uint64_t Mantissa(int32_t bits)
{
    return static_cast<uint64_t>(bits & 0x7FFFFF) | (1ULL << 23);
}

inline float MakeFloat(int exponent, uint64_t mantissa, bool negative = false)
{
    uint32_t bits = (static_cast<uint32_t>(exponent) << 23) | static_cast<uint32_t>(mantissa & 0x7FFFFF);
    if (negative)
        bits |= 0x80000000u;
    return std::bit_cast<float>(bits);
}

// C# semantics for `ulong >> int`: the count is masked to 6 bits.
inline uint64_t Shr(uint64_t v, int n)
{
    return v >> (n & 63);
}

inline int32_t Bits(float f) { return std::bit_cast<int32_t>(f); }

inline float Add(float a, float b)
{
    int32_t bitsA = Bits(a);
    int32_t bitsB = Bits(b);

    int expA = Exponent(bitsA);
    int expB = Exponent(bitsB);

    uint64_t manA = Mantissa(bitsA);
    uint64_t manB = Mantissa(bitsB);

    if (expA > expB) {
        manB = Shr(manB, expA - expB);
        expB = expA;
    } else if (expB > expA) {
        manA = Shr(manA, expB - expA);
        expA = expB;
    }

    uint64_t result = AddBits(manA, manB);

    if ((result & (1ULL << 24)) != 0) {
        result >>= 1;
        expA++;
    }

    return MakeFloat(expA, result);
}

inline float Subtract(float a, float b)
{
    int32_t bitsA = Bits(a);
    int32_t bitsB = Bits(b);

    bool negative = false;

    if (bitsB > bitsA) {
        int32_t temp = bitsA;
        bitsA = bitsB;
        bitsB = temp;
        negative = true;
    }

    int expA = Exponent(bitsA);
    int expB = Exponent(bitsB);

    uint64_t manA = Mantissa(bitsA);
    uint64_t manB = Mantissa(bitsB);

    manB = Shr(manB, expA - expB);

    uint64_t result = SubBits(manA, manB);

    if (result == 0)
        return 0;

    while ((result & (1ULL << 23)) == 0) {
        result <<= 1;
        expA--;
    }

    return MakeFloat(expA, result, negative);
}

inline float Multiply(float a, float b)
{
    int32_t bitsA = Bits(a);
    int32_t bitsB = Bits(b);

    int expA = Exponent(bitsA);
    int expB = Exponent(bitsB);

    uint64_t manA = Mantissa(bitsA);
    uint64_t manB = Mantissa(bitsB);

    uint64_t result = MulBits(manA, manB);

    int exponent = expA + expB - Bias;

    if ((result & (1ULL << 47)) != 0) {
        result >>= 24;
        exponent++;
    } else {
        result >>= 23;
    }

    return MakeFloat(exponent, result);
}

inline float Divide(float a, float b)
{
    int32_t bitsA = Bits(a);
    int32_t bitsB = Bits(b);

    int expA = Exponent(bitsA);
    int expB = Exponent(bitsB);

    uint64_t manA = Mantissa(bitsA);
    uint64_t manB = Mantissa(bitsB);

    manA <<= 23;

    uint64_t result = DivBits(manA, manB);

    int exponent = expA - expB + Bias;

    if (result < (1ULL << 23)) {
        result <<= 1;
        exponent--;
    }

    return MakeFloat(exponent, result);
}

} // namespace reference
