// Tests for the bit-serial integer primitives (AddBits / SubBits / MulBits / DivBits).
#include "test_framework.h"
#include "test_util.h"
#include "reference.h"

#include <arithmeticlib.h>

namespace {

struct Pair { uint64_t a, b; };

const Pair kPairs[] = {
    { 0, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 }, { 2, 3 }, { 7, 5 },
    { 0xFF, 1 }, { 0xFFFFFFFFull, 1 }, { 12345, 678 },
    { 0x800000, 0x800000 }, { 0xC00000, 0xA00000 }, { 0xFFFFFF, 0xFFFFFF },
    { 0xDEADBEEFull, 0xCAFEBABEull },
    { 0x123456789ABCDEF0ull, 0x0FEDCBA987654321ull },
    { 1ull << 63, 1 }, { 1ull << 63, 1ull << 63 },
    { ~0ull, 1 }, { ~0ull, ~0ull },
};

} // namespace

TEST(add_bits_matches_native_addition)
{
    for (const Pair& p : kPairs) {
        CHECK_EQ(arith_add_bits(p.a, p.b), p.a + p.b);
        CHECK_EQ(arith_add_bits(p.b, p.a), p.a + p.b);   // commutative
    }
}

TEST(add_bits_wraps_at_64_bits)
{
    CHECK_EQ(arith_add_bits(~0ull, 1), 0ull);
    CHECK_EQ(arith_add_bits(~0ull, ~0ull), ~0ull - 1);
    CHECK_EQ(arith_add_bits(1ull << 63, 1ull << 63), 0ull);
}

TEST(sub_bits_matches_native_subtraction)
{
    for (const Pair& p : kPairs) {
        CHECK_EQ(arith_sub_bits(p.a, p.b), p.a - p.b);
        CHECK_EQ(arith_sub_bits(p.b, p.a), p.b - p.a);
    }
}

TEST(sub_bits_wraps_below_zero)
{
    CHECK_EQ(arith_sub_bits(0, 1), ~0ull);
    CHECK_EQ(arith_sub_bits(5, 7), static_cast<uint64_t>(-2));
    CHECK_EQ(arith_sub_bits(0, 0), 0ull);
}

TEST(mul_bits_matches_native_multiplication)
{
    for (const Pair& p : kPairs) {
        CHECK_EQ(arith_mul_bits(p.a, p.b), p.a * p.b);   // native also wraps mod 2^64
        CHECK_EQ(arith_mul_bits(p.b, p.a), p.a * p.b);
    }
}

TEST(mul_bits_identities)
{
    CHECK_EQ(arith_mul_bits(0, 0xDEADBEEFull), 0ull);
    CHECK_EQ(arith_mul_bits(0xDEADBEEFull, 0), 0ull);
    CHECK_EQ(arith_mul_bits(1, 0xDEADBEEFull), 0xDEADBEEFull);
    CHECK_EQ(arith_mul_bits(0xDEADBEEFull, 1), 0xDEADBEEFull);
    CHECK_EQ(arith_mul_bits(0x800000, 0x800000), 1ull << 46);   // 1.0 * 1.0 mantissas
}

TEST(div_bits_matches_native_division)
{
    for (const Pair& p : kPairs) {
        if (p.b != 0) CHECK_EQ(arith_div_bits(p.a, p.b), p.a / p.b);
        if (p.a != 0) CHECK_EQ(arith_div_bits(p.b, p.a), p.b / p.a);
    }
    CHECK_EQ(arith_div_bits(1ull << 46, 0xC00000), (1ull << 46) / 0xC00000);   // 1.0 / 1.5 mantissas
    CHECK_EQ(arith_div_bits(~0ull, 1), ~0ull);
    CHECK_EQ(arith_div_bits(~0ull, ~0ull), 1ull);
}

TEST(div_bits_by_zero_yields_all_ones)
{
    // The remainder is always >= 0, so every quotient bit gets set (reference behaviour).
    CHECK_EQ(arith_div_bits(5, 0), ~0ull);
    CHECK_EQ(arith_div_bits(0, 0), ~0ull);
}

TEST(primitives_agree_with_reference_and_native_on_random_inputs)
{
    testutil::Rng rng(0xA11CE5EEDull);
    for (int i = 0; i < 2000; ++i) {
        const uint64_t a = rng.next();
        const uint64_t b = rng.next() >> rng.between(0, 63);   // vary the magnitude of b

        CHECK_EQ(arith_add_bits(a, b), reference::AddBits(a, b));
        CHECK_EQ(arith_sub_bits(a, b), reference::SubBits(a, b));
        CHECK_EQ(arith_mul_bits(a, b), reference::MulBits(a, b));
        CHECK_EQ(arith_div_bits(a, b), reference::DivBits(a, b));

        CHECK_EQ(arith_add_bits(a, b), a + b);
        CHECK_EQ(arith_sub_bits(a, b), a - b);
        CHECK_EQ(arith_mul_bits(a, b), a * b);
        if (b != 0)
            CHECK_EQ(arith_div_bits(a, b), a / b);
    }
}
