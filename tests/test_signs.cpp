// Tests for signed operands: sign rules, signed zero, cancellation,
// division by zero, and the shortcut path that skips the ALU.
#include "test_framework.h"
#include "test_util.h"
#include "reference.h"

#include <arithmeticlib.h>
#include <cmath>

using testutil::Machine;
using testutil::fromBits;
using testutil::regs;
using testutil::toBits;

namespace {

struct Case { float a, b; };

// Pairs whose sum and difference are representable in binary32, so the
// truncating algorithm and native rounding have to agree exactly.
const Case kAdditive[] = {
    { 1.5f, 2.25f }, { 5.f, 3.f }, { 0.5f, 0.25f }, { 8.f, 0.5f },
    { 1024.f, 1.f }, { 100.f, 0.125f }, { 3.75f, 1.5f }, { 2.f, 1.f },
    { 7.f, 7.f },                                  // equal magnitudes: cancellation
};

const Case kProducts[] = {
    { 1.5f, 1.5f }, { 3.f, 4.f }, { 0.5f, 0.5f }, { 1.25f, 8.f }, { 7.f, 7.f }, { 2.f, 3.f },
};

const Case kQuotients[] = {
    { 6.f, 3.f }, { 1.f, 2.f }, { 10.f, 4.f }, { 7.f, 8.f }, { 2.25f, 1.5f }, { 9.f, 3.f },
};

const float kNegativeZero = -0.0f;

} // namespace

TEST(add_handles_every_sign_combination)
{
    for (const Case& c : kAdditive)
        for (int sa = 0; sa < 2; ++sa)
            for (int sb = 0; sb < 2; ++sb) {
                const float a = sa ? -c.a : c.a;
                const float b = sb ? -c.b : c.b;
                CHECK_FLOAT_BITS(arith_add(a, b), a + b);
                CHECK_FLOAT_BITS(arith_add(b, a), a + b);      // still commutative
            }
}

TEST(subtract_handles_every_sign_combination)
{
    for (const Case& c : kAdditive)
        for (int sa = 0; sa < 2; ++sa)
            for (int sb = 0; sb < 2; ++sb) {
                const float a = sa ? -c.a : c.a;
                const float b = sb ? -c.b : c.b;
                CHECK_FLOAT_BITS(arith_subtract(a, b), a - b);
                CHECK_FLOAT_BITS(arith_subtract(b, a), b - a);
            }
}

TEST(multiply_and_divide_combine_the_signs)
{
    for (const Case& c : kProducts)
        for (int sa = 0; sa < 2; ++sa)
            for (int sb = 0; sb < 2; ++sb) {
                const float a = sa ? -c.a : c.a;
                const float b = sb ? -c.b : c.b;
                CHECK_FLOAT_BITS(arith_multiply(a, b), a * b);
                CHECK_FLOAT_BITS(arith_multiply(b, a), a * b);
            }

    for (const Case& c : kQuotients)
        for (int sa = 0; sa < 2; ++sa)
            for (int sb = 0; sb < 2; ++sb) {
                const float a = sa ? -c.a : c.a;
                const float b = sb ? -c.b : c.b;
                CHECK_FLOAT_BITS(arith_divide(a, b), a / b);
            }
}

TEST(subtracting_is_adding_the_negation)
{
    testutil::Rng rng(11);
    for (int i = 0; i < 3000; ++i) {
        const int32_t expA = rng.between(100, 150);
        const float a = testutil::randomSigned(rng, expA, expA);
        const float b = testutil::randomSigned(rng, expA - 50, expA + 50);
        CHECK_FLOAT_BITS(arith_subtract(a, b), arith_add(a, -b));
        CHECK_FLOAT_BITS(arith_subtract(a, -b), arith_add(a, b));
    }
}

TEST(negating_an_operand_negates_the_result)
{
    const float a = 7.5f, b = 2.5f;
    CHECK_FLOAT_BITS(arith_multiply(-a, b), -arith_multiply(a, b));
    CHECK_FLOAT_BITS(arith_multiply(a, -b), -arith_multiply(a, b));
    CHECK_FLOAT_BITS(arith_multiply(-a, -b), arith_multiply(a, b));
    CHECK_FLOAT_BITS(arith_divide(-a, b), -arith_divide(a, b));
    CHECK_FLOAT_BITS(arith_divide(a, -b), -arith_divide(a, b));
    CHECK_FLOAT_BITS(arith_divide(-a, -b), arith_divide(a, b));
    CHECK_FLOAT_BITS(arith_add(-a, -b), -arith_add(a, b));
    CHECK_FLOAT_BITS(arith_subtract(-a, -b), -arith_subtract(a, b));
}

TEST(zero_minus_x_is_minus_x)
{
    const float values[] = { 1.f, 2.5f, 1024.f, 0.125f, -3.f, -0.75f };
    for (float x : values) {
        CHECK_FLOAT_BITS(arith_subtract(0.f, x), -x);
        CHECK_FLOAT_BITS(arith_add(0.f, x), x);
        CHECK_FLOAT_BITS(arith_add(x, 0.f), x);
        CHECK_FLOAT_BITS(arith_subtract(x, 0.f), x);
    }
}

TEST(equal_magnitudes_of_unlike_sign_cancel_to_positive_zero)
{
    const float values[] = { 1.f, 2.5f, 1e20f, 1e-20f, 0.125f };
    for (float x : values) {
        CHECK_EQ(toBits(arith_add(x, -x)), 0u);
        CHECK_EQ(toBits(arith_add(-x, x)), 0u);
        CHECK_EQ(toBits(arith_subtract(x, x)), 0u);
        CHECK_EQ(toBits(arith_subtract(-x, -x)), 0u);
    }
}

TEST(signed_zero_operands_match_ieee)
{
    const float zeros[] = { 0.f, kNegativeZero };
    for (float a : zeros)
        for (float b : zeros) {
            CHECK_FLOAT_BITS(arith_add(a, b), a + b);
            CHECK_FLOAT_BITS(arith_subtract(a, b), a - b);
            CHECK_FLOAT_BITS(arith_multiply(a, b), a * b);
        }

    // Spot-check the cases the rules above turn on.
    CHECK_EQ(toBits(arith_add(0.f, kNegativeZero)), 0u);                // +0
    CHECK_EQ(toBits(arith_add(kNegativeZero, kNegativeZero)), 0x80000000u);
    CHECK_EQ(toBits(arith_subtract(kNegativeZero, 0.f)), 0x80000000u);
    CHECK_EQ(toBits(arith_subtract(0.f, kNegativeZero)), 0u);
}

TEST(negative_zero_keeps_its_sign_through_multiply_and_divide)
{
    CHECK_FLOAT_BITS(arith_multiply(kNegativeZero, 5.f), -0.0f);
    CHECK_FLOAT_BITS(arith_multiply(5.f, kNegativeZero), -0.0f);
    CHECK_FLOAT_BITS(arith_multiply(kNegativeZero, -5.f), 0.0f);
    CHECK_FLOAT_BITS(arith_multiply(0.f, -5.f), -0.0f);
    CHECK_FLOAT_BITS(arith_divide(kNegativeZero, 5.f), -0.0f);
    CHECK_FLOAT_BITS(arith_divide(0.f, -5.f), -0.0f);
    CHECK_FLOAT_BITS(arith_divide(kNegativeZero, -5.f), 0.0f);
}

TEST(adding_zero_preserves_the_other_operand_exactly)
{
    // Pass-through copies the operand's bit pattern, so even values the
    // algorithm cannot decompose survive untouched.
    const uint32_t patterns[] = { 0x3F800001u, 0x00000001u, 0x007FFFFFu, 0x7F7FFFFFu };
    for (uint32_t bits : patterns) {
        const float x = fromBits(bits);
        CHECK_EQ(toBits(arith_add(x, 0.f)), bits);
        CHECK_EQ(toBits(arith_add(0.f, x)), bits);
        CHECK_EQ(toBits(arith_subtract(x, 0.f)), bits);
        CHECK_EQ(toBits(arith_add(-x, 0.f)), bits | 0x80000000u);
    }
}

TEST(divide_by_zero_gives_infinity_and_zero_over_zero_gives_nan)
{
    CHECK_FLOAT_BITS(arith_divide(1.f, 0.f), INFINITY);
    CHECK_FLOAT_BITS(arith_divide(-1.f, 0.f), -INFINITY);
    CHECK_FLOAT_BITS(arith_divide(1.f, kNegativeZero), -INFINITY);
    CHECK_FLOAT_BITS(arith_divide(-1.f, kNegativeZero), INFINITY);
    CHECK_FLOAT_BITS(arith_divide(1e20f, 0.f), INFINITY);

    CHECK(std::isnan(arith_divide(0.f, 0.f)));
    CHECK(std::isnan(arith_divide(kNegativeZero, 0.f)));
    CHECK(std::isnan(arith_divide(0.f, kNegativeZero)));
}

TEST(machine_reports_the_signs_and_the_magnitude_operation)
{
    {   // unlike signs make an addition into a magnitude subtraction, and the
        // larger magnitude is moved into a.
        Machine m(ARITH_OP_ADD, -1.5f, 2.25f);
        arith_machine_step(m);                         // Unpack
        const ArithRegisters r = regs(m);
        CHECK_EQ(r.mag_op, ARITH_MAG_SUB);
        CHECK_EQ(r.bits_a, static_cast<int32_t>(toBits(2.25f)));
        CHECK_EQ(r.sign_a, 0);
        CHECK_EQ(r.bits_b, static_cast<int32_t>(toBits(1.5f)));
        CHECK_EQ(r.sign_b, 1);
        CHECK_EQ(r.negative, 0);
        CHECK_FLOAT_BITS(arith_machine_run(m), 0.75f);
    }
    {   // subtracting a negative operand is an addition
        Machine m(ARITH_OP_SUB, 1.5f, -2.25f);
        arith_machine_step(m);
        const ArithRegisters r = regs(m);
        CHECK_EQ(r.mag_op, ARITH_MAG_ADD);
        CHECK_EQ(r.sign_a, 0);
        CHECK_EQ(r.sign_b, 0);                         // flipped from the raw -2.25
        CHECK_EQ(r.negative, 0);
        CHECK_FLOAT_BITS(arith_machine_run(m), 3.75f);
    }
    {   // both negative: magnitudes add, the result is negative
        Machine m(ARITH_OP_ADD, -1.5f, -2.25f);
        arith_machine_step(m);
        const ArithRegisters r = regs(m);
        CHECK_EQ(r.mag_op, ARITH_MAG_ADD);
        CHECK_EQ(r.sign_a, 1);
        CHECK_EQ(r.sign_b, 1);
        CHECK_EQ(r.negative, 1);
        CHECK_FLOAT_BITS(arith_machine_run(m), -3.75f);
    }
    {   // multiply exclusive-ors the signs and leaves the operands in place
        Machine m(ARITH_OP_MUL, 3.f, -4.f);
        arith_machine_step(m);
        const ArithRegisters r = regs(m);
        CHECK_EQ(r.mag_op, ARITH_MAG_MUL);
        CHECK_EQ(r.bits_a, static_cast<int32_t>(toBits(3.f)));
        CHECK_EQ(r.bits_b, static_cast<int32_t>(toBits(4.f)));
        CHECK_EQ(r.sign_b, 1);
        CHECK_EQ(r.negative, 1);
        CHECK_FLOAT_BITS(arith_machine_run(m), -12.f);
    }
}

TEST(trivial_results_skip_the_alu)
{
    // A zero operand, cancellation and division by zero are all decided in
    // Unpack, so the machine goes straight to Pack: three cycles, no ALU.
    const struct { ArithOp op; float a, b; float expected; } cases[] = {
        { ARITH_OP_ADD, 2.5f,  0.f,   2.5f },
        { ARITH_OP_SUB, 2.5f,  2.5f,  0.f },
        { ARITH_OP_MUL, 2.5f,  0.f,   0.f },
        { ARITH_OP_DIV, 0.f,   2.5f,  0.f },
    };
    for (const auto& c : cases) {
        Machine m(c.op, c.a, c.b);
        arith_machine_step(m);
        CHECK_EQ(arith_machine_state(m), ARITH_STATE_PACK);
        CHECK_EQ(regs(m).alu_phase, ARITH_ALU_IDLE);
        CHECK_EQ(regs(m).alu_cycles, 0u);
        CHECK_FLOAT_BITS(arith_machine_run(m), c.expected);
        CHECK_EQ(arith_machine_cycles(m), 2u);         // Unpack then Pack, nothing else
    }
}

// Exponents are kept in a range where the result exponent of every operation
// stays inside [1, 254] and the alignment shift stays below 64, the two places
// where the algorithm is deliberately outside IEEE-754 territory.
TEST(signed_operations_agree_with_the_reference)
{
    testutil::Rng rng(21);
    for (int i = 0; i < 5000; ++i) {
        const int32_t expA = rng.between(100, 150);
        const float a = testutil::randomSigned(rng, expA, expA);
        const float b = testutil::randomSigned(rng, expA - 50, expA + 50);

        CHECK_FLOAT_BITS(arith_add(a, b), reference::AddSigned(a, b));
        CHECK_FLOAT_BITS(arith_subtract(a, b), reference::SubtractSigned(a, b));
        CHECK_FLOAT_BITS(arith_multiply(a, b), reference::MultiplySigned(a, b));
        CHECK_FLOAT_BITS(arith_divide(a, b), reference::DivideSigned(a, b));
    }
}

// An independent check of the sign logic that does not go through the
// reference: whatever truncation does to the magnitude, the sign has to be the
// one the hardware produces.
//
// Where the only truncation is of the result itself (magnitude addition,
// multiply, divide) the magnitude can also be bounded: it never exceeds the
// correctly rounded one. A magnitude *subtraction* truncates the subtrahend
// instead, which makes the difference too large rather than too small, so it
// gets no bound here - `subtract_has_no_guard_bit` covers that case.
TEST(signs_match_the_hardware)
{
    testutil::Rng rng(31);
    for (int i = 0; i < 3000; ++i) {
        const int32_t expA = rng.between(100, 150);
        const float a = testutil::randomSigned(rng, expA, expA);
        const float b = testutil::randomSigned(rng, expA - 50, expA + 50);
        const bool likeSigns = std::signbit(a) == std::signbit(b);

        const struct { float ours, native; bool boundMagnitude; } rows[] = {
            { arith_add(a, b),      a + b, likeSigns  },
            { arith_subtract(a, b), a - b, !likeSigns },
            { arith_multiply(a, b), a * b, true       },
            { arith_divide(a, b),   a / b, true       },
        };
        for (const auto& row : rows) {
            const uint32_t ours = toBits(row.ours), native = toBits(row.native);
            CHECK_EQ(ours >> 31, native >> 31);
            if (row.boundMagnitude)
                CHECK((ours & 0x7FFFFFFFu) <= (native & 0x7FFFFFFFu));
        }
    }
}

TEST(signed_zero_operands_agree_with_the_reference)
{
    testutil::Rng rng(22);
    const float zeros[] = { 0.f, kNegativeZero };
    for (int i = 0; i < 200; ++i) {
        const float x = testutil::randomSigned(rng, 64, 190);
        for (float z : zeros) {
            CHECK_FLOAT_BITS(arith_add(x, z), reference::AddSigned(x, z));
            CHECK_FLOAT_BITS(arith_add(z, x), reference::AddSigned(z, x));
            CHECK_FLOAT_BITS(arith_subtract(x, z), reference::SubtractSigned(x, z));
            CHECK_FLOAT_BITS(arith_subtract(z, x), reference::SubtractSigned(z, x));
            CHECK_FLOAT_BITS(arith_multiply(x, z), reference::MultiplySigned(x, z));
            CHECK_FLOAT_BITS(arith_divide(z, x), reference::DivideSigned(z, x));
            CHECK_FLOAT_BITS(arith_divide(x, z), reference::DivideSigned(x, z));
        }
    }
}
