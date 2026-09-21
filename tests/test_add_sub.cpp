// Tests for arith_add / arith_subtract.
//
// "Exact" cases are ones whose true result is representable in binary32, so
// the truncating algorithm and IEEE-754 round-to-nearest must agree bit for bit.
#include "test_framework.h"
#include "test_util.h"

#include <arithmeticlib.h>
#include <cmath>

using testutil::fromBits;
using testutil::toBits;

namespace {
struct Case { float a, b; };
}

TEST(add_exact_results_match_native)
{
    const Case cases[] = {
        { 1.f, 1.f }, { 1.5f, 2.25f }, { 0.5f, 0.25f }, { 3.f, 5.f },
        { 1.5f, 1.5f }, { 1.75f, 1.75f }, { 8.f, 0.5f }, { 1024.f, 1.f },
        { 8388608.f, 1.f }, { 100.f, 0.125f }, { 1e10f, 1e10f }, { 65536.f, 0.5f },
        { 0.75f, 0.375f }, { 7.5f, 0.0625f }, { 16777216.f, 1.f }, { 1e-20f, 1e-20f },
    };
    for (const Case& c : cases) {
        CHECK_FLOAT_BITS(arith_add(c.a, c.b), c.a + c.b);
        CHECK_FLOAT_BITS(arith_add(c.b, c.a), c.a + c.b);   // commutative
    }
}

TEST(add_carry_out_bumps_the_exponent)
{
    CHECK_FLOAT_BITS(arith_add(1.5f, 1.5f), 3.f);
    // (2 - 2^-23) + (2 - 2^-23) = 4 - 2^-22, the mantissa sum overflows bit 24.
    CHECK_FLOAT_BITS(arith_add(fromBits(0x3FFFFFFF), fromBits(0x3FFFFFFF)), fromBits(0x407FFFFF));
}

TEST(add_truncates_bits_shifted_out_of_the_smaller_operand)
{
    // 1 + 1.5 * 2^-24: IEEE rounds up to 1 + 2^-23, the algorithm drops the bits.
    const float tiny = std::ldexp(1.5f, -24);
    CHECK_FLOAT_BITS(arith_add(1.f, tiny), 1.f);
    CHECK_FLOAT_BITS(1.f + tiny, fromBits(0x3F800001));   // what native rounding does
}

TEST(add_far_apart_operands_returns_the_larger_one)
{
    // Exponent difference >= 64. C# would mask the shift count to 6 bits and
    // add garbage; the DLL flushes the small operand to zero.
    const float big = std::ldexp(1.f, 70);
    CHECK_FLOAT_BITS(arith_add(big, 1.f), big);
    CHECK_FLOAT_BITS(arith_add(1.f, big), big);
    CHECK_FLOAT_BITS(arith_add(big, 1.f), big + 1.f);
}

TEST(subtract_exact_results_match_native)
{
    const Case cases[] = {
        { 5.f, 3.f }, { 3.75f, 1.5f }, { 0.5f, 0.25f }, { 1024.f, 1.f },
        { 100.f, 99.5f }, { 2.f, 1.f }, { 1.f, 0.9375f }, { 10.f, 0.5f },
        { 1.5f, 1.25f }, { 8.f, 7.5f }, { 3e9f, 1e9f }, { 3.f, 0.75f },
    };
    for (const Case& c : cases)
        CHECK_FLOAT_BITS(arith_subtract(c.a, c.b), c.a - c.b);
}

TEST(subtract_smaller_minus_larger_is_negative)
{
    CHECK_FLOAT_BITS(arith_subtract(2.f, 3.f), -1.f);
    CHECK_FLOAT_BITS(arith_subtract(1.5f, 4.f), -2.5f);
    CHECK_FLOAT_BITS(arith_subtract(0.25f, 1.f), -0.75f);
    CHECK_FLOAT_BITS(arith_subtract(99.5f, 100.f), -0.5f);
}

TEST(subtract_equal_operands_gives_positive_zero)
{
    CHECK_EQ(toBits(arith_subtract(1.f, 1.f)), 0u);
    CHECK_EQ(toBits(arith_subtract(2.5f, 2.5f)), 0u);
    CHECK_EQ(toBits(arith_subtract(1e-30f, 1e-30f)), 0u);
}

TEST(subtract_renormalizes_after_cancellation)
{
    // 1 - 2^-23: the difference is 0x7FFFFF and needs one left shift.
    CHECK_FLOAT_BITS(arith_subtract(1.f, std::ldexp(1.f, -23)), fromBits(0x3F7FFFFE));
    CHECK_FLOAT_BITS(arith_subtract(1.f, std::ldexp(1.f, -23)), 1.f - std::ldexp(1.f, -23));

    // 1 - 0.9375 = 0.0625: four leading zeros to shift out.
    CHECK_FLOAT_BITS(arith_subtract(1.f, 0.9375f), 0.0625f);

    // 100 - 99.5: same exponent, seven leading zeros.
    CHECK_FLOAT_BITS(arith_subtract(100.f, 99.5f), 0.5f);
}

TEST(subtract_has_no_guard_bit)
{
    // 1 - (1 - 2^-24): aligning b shifts its lowest set bit out, so the exact
    // answer 2^-24 comes out as 2^-23 (native gets it right).
    const float belowOne = fromBits(0x3F7FFFFF);
    CHECK_FLOAT_BITS(arith_subtract(1.f, belowOne), std::ldexp(1.f, -23));
    CHECK_FLOAT_BITS(1.f - belowOne, std::ldexp(1.f, -24));
}
