// reference.h : line-for-line transliteration of the C# ArithmeticModule.
//
// The four Add/Subtract/Multiply/Divide functions are the C# original and so
// handle positive magnitudes only; a signed layer on top (AddSigned, ...)
// adds the sign rules. Together they are the specification the DLL is tested
// against. The transliteration is deliberately *not* a state machine and keeps
// the C# semantics, including the masked shift count (`ulong >> n` in C#
// shifts by `n & 63`).
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
// ---------------------------------------------------------------------------
// Signed layer.
//
// The four functions above are the C# original and operate on positive
// magnitudes. The sign rules below are written independently of the DLL's
// state machine - decide the sign here, delegate the magnitude to the
// original code - so the two implementations can be compared.

inline bool  SignOf(float f)      { return (std::bit_cast<uint32_t>(f) >> 31) != 0; }
inline bool  IsZero(float f)      { return (std::bit_cast<uint32_t>(f) & 0x7FFFFFFFu) == 0; }
inline float MagnitudeOf(float f) { return std::bit_cast<float>(std::bit_cast<uint32_t>(f) & 0x7FFFFFFFu); }
inline float Negate(float f)      { return std::bit_cast<float>(std::bit_cast<uint32_t>(f) ^ 0x80000000u); }

inline float WithSign(float magnitude, bool negative)
{
    uint32_t bits = std::bit_cast<uint32_t>(magnitude) & 0x7FFFFFFFu;
    if (negative)
        bits |= 0x80000000u;
    return std::bit_cast<float>(bits);
}

inline float Zero(bool negative)     { return WithSign(0.0f, negative); }
inline float Infinity(bool negative) { return WithSign(std::bit_cast<float>(0x7F800000u), negative); }
inline float QuietNan()              { return std::bit_cast<float>(0x7FC00000u); }

inline float AddSigned(float a, float b)
{
    const bool  signA = SignOf(a),      signB = SignOf(b);
    const float magA  = MagnitudeOf(a), magB  = MagnitudeOf(b);

    if (signA == signB) {                       // like signs: magnitudes add
        if (IsZero(a)) return WithSign(magB, signA);
        if (IsZero(b)) return WithSign(magA, signA);
        return WithSign(Add(magA, magB), signA);
    }

    // Unlike signs: magnitudes subtract and the larger one keeps its sign.
    const uint32_t bitsA = std::bit_cast<uint32_t>(magA);
    const uint32_t bitsB = std::bit_cast<uint32_t>(magB);
    if (bitsA == bitsB)
        return Zero(false);                     // cancellation -> +0
    if (bitsA > bitsB)
        return IsZero(b) ? WithSign(magA, signA) : WithSign(Subtract(magA, magB), signA);
    return IsZero(a) ? WithSign(magB, signB) : WithSign(Subtract(magB, magA), signB);
}

inline float SubtractSigned(float a, float b)
{
    return AddSigned(a, Negate(b));             // a - b == a + (-b)
}

inline float MultiplySigned(float a, float b)
{
    const bool negative = SignOf(a) != SignOf(b);
    if (IsZero(a) || IsZero(b))
        return Zero(negative);
    return WithSign(Multiply(MagnitudeOf(a), MagnitudeOf(b)), negative);
}

inline float DivideSigned(float a, float b)
{
    const bool negative = SignOf(a) != SignOf(b);
    if (IsZero(b))
        return IsZero(a) ? QuietNan() : Infinity(negative);
    if (IsZero(a))
        return Zero(negative);
    return WithSign(Divide(MagnitudeOf(a), MagnitudeOf(b)), negative);
}

} // namespace reference
