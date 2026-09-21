// Tests for arith_multiply / arith_divide, plus the arith_compute dispatcher
// and the randomized comparison against the C# reference.
#include "test_framework.h"
#include "test_util.h"
#include "reference.h"

#include <arithmeticlib.h>
#include <cmath>

using testutil::fromBits;
using testutil::toBits;

namespace {
struct Case { float a, b; };
}

TEST(multiply_exact_results_match_native)
{
    const Case cases[] = {
        { 2.f, 3.f }, { 1.5f, 1.5f }, { 0.5f, 0.5f }, { 3.f, 4.f }, { 1.25f, 8.f },
        { 7.f, 7.f }, { 1024.f, 1024.f }, { 0.1f, 10.f }, { 3.5f, 2.f },
        { fromBits(0x3FFFFFFF), 2.f }, { 4095.f, 4097.f }, { 0.75f, 0.75f },
        { 255.f, 255.f }, { std::ldexp(1.f, 60), std::ldexp(1.f, 60) },
        { std::ldexp(1.f, -60), std::ldexp(1.f, -60) }, { 1.f, 1.f },
    };
    for (const Case& c : cases) {
        CHECK_FLOAT_BITS(arith_multiply(c.a, c.b), c.a * c.b);
        CHECK_FLOAT_BITS(arith_multiply(c.b, c.a), c.a * c.b);   // commutative
    }
}

TEST(multiply_normalizes_both_product_ranges)
{
    CHECK_FLOAT_BITS(arith_multiply(1.25f, 1.5f), 1.875f);   // product in [1, 2)
    CHECK_FLOAT_BITS(arith_multiply(1.5f, 1.5f), 2.25f);     // product in [2, 4): exponent + 1
}

TEST(multiply_truncates_instead_of_rounding)
{
    // 1.5 * (1 + 2^-23) = 1.5 + 2^-23 + 2^-24: exactly halfway between two floats.
    // IEEE rounds to even (0x3FC00002); the algorithm drops the low bits (0x3FC00001).
    CHECK_FLOAT_BITS(arith_multiply(1.5f, fromBits(0x3F800001)), fromBits(0x3FC00001));
    CHECK_EQ(toBits(1.5f * fromBits(0x3F800001)), 0x3FC00002u);
}

TEST(divide_exact_results_match_native)
{
    const Case cases[] = {
        { 6.f, 3.f }, { 1.f, 2.f }, { 10.f, 4.f }, { 7.f, 8.f }, { 1.f, 8.f },
        { 9.f, 3.f }, { 100.f, 10.f }, { 2.25f, 1.5f }, { 1.f, 1.f }, { 3.f, 1.5f },
        { 0.75f, 3.f }, { 1e10f, 1e10f }, { 65536.f, 256.f }, { 1.5f, 0.5f },
        { fromBits(0x3FFFFFFF), 1.f }, { 12.f, 3.f }, { 1.f, 4.f },
        { std::ldexp(1.f, 60), std::ldexp(1.f, -60) },
    };
    for (const Case& c : cases)
        CHECK_FLOAT_BITS(arith_divide(c.a, c.b), c.a / c.b);
}

TEST(divide_truncates_instead_of_rounding)
{
    CHECK_FLOAT_BITS(arith_divide(1.f, 3.f), fromBits(0x3EAAAAAA));
    CHECK_EQ(toBits(1.f / 3.f), 0x3EAAAAABu);                    // IEEE rounds up

    CHECK_FLOAT_BITS(arith_divide(2.f, 3.f), fromBits(0x3F2AAAAA));
    CHECK_EQ(toBits(2.f / 3.f), 0x3F2AAAABu);
}

TEST(compute_dispatches_on_op)
{
    CHECK_FLOAT_BITS(arith_compute(ARITH_OP_ADD, 1.5f, 2.25f), arith_add(1.5f, 2.25f));
    CHECK_FLOAT_BITS(arith_compute(ARITH_OP_SUB, 1.5f, 2.25f), arith_subtract(1.5f, 2.25f));
    CHECK_FLOAT_BITS(arith_compute(ARITH_OP_MUL, 1.5f, 2.25f), arith_multiply(1.5f, 2.25f));
    CHECK_FLOAT_BITS(arith_compute(ARITH_OP_DIV, 1.5f, 2.25f), arith_divide(1.5f, 2.25f));
}

// Randomized comparison against the transliterated C# module. Exponents are
// kept in a range where neither the exponent arithmetic overflows nor the
// alignment shift reaches 64 (where the DLL deliberately differs from C#).
TEST(add_agrees_with_reference_on_random_normals)
{
    testutil::Rng rng(1);
    for (int i = 0; i < 5000; ++i) {
        const int32_t expA = rng.between(64, 190);
        const float a = testutil::makeFloat(expA, static_cast<uint32_t>(rng.next()));
        const float b = testutil::randomNormal(rng, expA - 60, expA + 60);
        CHECK_FLOAT_BITS(arith_add(a, b), reference::Add(a, b));
    }
}

TEST(subtract_agrees_with_reference_on_random_normals)
{
    testutil::Rng rng(2);
    for (int i = 0; i < 5000; ++i) {
        const int32_t expA = rng.between(64, 190);
        const float a = testutil::makeFloat(expA, static_cast<uint32_t>(rng.next()));
        const float b = testutil::randomNormal(rng, expA - 60, expA + 60);
        CHECK_FLOAT_BITS(arith_subtract(a, b), reference::Subtract(a, b));
        CHECK_FLOAT_BITS(arith_subtract(b, a), reference::Subtract(b, a));
    }
}

TEST(multiply_agrees_with_reference_on_random_normals)
{
    testutil::Rng rng(3);
    for (int i = 0; i < 5000; ++i) {
        const float a = testutil::randomNormal(rng, 64, 190);
        const float b = testutil::randomNormal(rng, 64, 190);
        CHECK_FLOAT_BITS(arith_multiply(a, b), reference::Multiply(a, b));
    }
}

TEST(divide_agrees_with_reference_on_random_normals)
{
    testutil::Rng rng(4);
    for (int i = 0; i < 5000; ++i) {
        const float a = testutil::randomNormal(rng, 65, 190);
        const float b = testutil::randomNormal(rng, 65, 190);
        CHECK_FLOAT_BITS(arith_divide(a, b), reference::Divide(a, b));
    }
}

TEST(results_stay_within_truncation_error_of_native_for_random_normals)
{
    // Truncation instead of rounding costs at most one unit in the last place
    // for multiply and add. Divide can be off by two: when the quotient is
    // below 1 the reference algorithm left-shifts a 23-bit truncated quotient,
    // so the last bit of the result is always a padded zero.
    testutil::Rng rng(5);
    for (int i = 0; i < 2000; ++i) {
        const float a = testutil::randomNormal(rng, 100, 150);
        const float b = testutil::randomNormal(rng, 100, 150);

        const uint32_t mul = toBits(arith_multiply(a, b)), nmul = toBits(a * b);
        const uint32_t div = toBits(arith_divide(a, b)),   ndiv = toBits(a / b);
        const uint32_t add = toBits(arith_add(a, b)),      nadd = toBits(a + b);

        CHECK(nmul - mul <= 1u);
        CHECK(ndiv - div <= 2u);
        CHECK(nadd - add <= 1u);
    }
}
