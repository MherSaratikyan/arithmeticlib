// Tests for the state sequence and cycle counts of the stepwise machine.
#include "test_framework.h"
#include "test_util.h"

#include <arithmeticlib.h>
#include <vector>

using testutil::Machine;
using testutil::count;
using testutil::regs;
using testutil::trace;

namespace {
const ArithOp kOps[] = { ARITH_OP_ADD, ARITH_OP_SUB, ARITH_OP_MUL, ARITH_OP_DIV };
}

TEST(machine_starts_in_unpack_with_no_result)
{
    Machine m(ARITH_OP_ADD, 1.f, 2.f);
    CHECK_EQ(arith_machine_state(m), ARITH_STATE_UNPACK);
    CHECK_EQ(arith_machine_is_done(m), 0);
    CHECK_EQ(arith_machine_cycles(m), 0u);
    CHECK_FLOAT_BITS(arith_machine_result(m), 0.f);
    CHECK_EQ(regs(m).alu_phase, ARITH_ALU_IDLE);
}

TEST(machine_visits_the_pipeline_stages_in_order)
{
    for (ArithOp op : kOps) {
        Machine m(op, 6.f, 1.5f);
        const std::vector<ArithState> t = trace(m);

        CHECK(t.size() >= 5);
        CHECK_EQ(t.front(), ARITH_STATE_UNPACK);
        CHECK_EQ(t[1], ARITH_STATE_ALIGN);
        CHECK_EQ(t.back(), ARITH_STATE_PACK);
        CHECK_EQ(count(t, ARITH_STATE_UNPACK), 1);
        CHECK_EQ(count(t, ARITH_STATE_ALIGN), 1);
        CHECK(count(t, ARITH_STATE_COMPUTE) >= 1);
        CHECK(count(t, ARITH_STATE_NORMALIZE) >= 1);
        CHECK_EQ(count(t, ARITH_STATE_PACK), 1);
        CHECK_EQ(count(t, ARITH_STATE_DONE), 0);

        for (size_t i = 1; i < t.size(); ++i)          // stages never go backwards
            CHECK(t[i] >= t[i - 1]);

        CHECK_EQ(arith_machine_state(m), ARITH_STATE_DONE);
        CHECK_EQ(arith_machine_cycles(m), static_cast<uint32_t>(t.size()));
        CHECK_FLOAT_BITS(arith_machine_result(m), arith_compute(op, 6.f, 1.5f));
    }
}

TEST(multiply_compute_takes_one_cycle_per_multiplier_bit)
{
    Machine m(ARITH_OP_MUL, 3.f, 4.f);
    const std::vector<ArithState> t = trace(m);
    CHECK_EQ(count(t, ARITH_STATE_COMPUTE), 24);       // 24-bit mantissa
    CHECK_EQ(arith_machine_cycles(m), 28u);            // 1 + 1 + 24 + 1 + 1
    CHECK_FLOAT_BITS(arith_machine_result(m), 12.f);
}

TEST(divide_compute_takes_one_cycle_per_dividend_bit)
{
    Machine m(ARITH_OP_DIV, 10.f, 4.f);
    const std::vector<ArithState> t = trace(m);
    CHECK_EQ(count(t, ARITH_STATE_COMPUTE), 64);       // restoring division over 64 bits
    CHECK_EQ(arith_machine_cycles(m), 68u);
    CHECK_FLOAT_BITS(arith_machine_result(m), 2.5f);
}

TEST(add_compute_ripples_the_carry_chain)
{
    // 0x800000 + 0x800000: one carry ripples out of bit 23 into bit 24, then settles.
    Machine m(ARITH_OP_ADD, 1.f, 1.f);
    const std::vector<ArithState> t = trace(m);
    CHECK_EQ(count(t, ARITH_STATE_COMPUTE), 2);
    CHECK_EQ(arith_machine_cycles(m), 6u);
    CHECK_FLOAT_BITS(arith_machine_result(m), 2.f);
}

TEST(subtract_negates_then_adds)
{
    Machine m(ARITH_OP_SUB, 3.f, 1.f);
    arith_machine_step(m);                             // Unpack
    arith_machine_step(m);                             // Align -> ALU loaded
    CHECK_EQ(arith_machine_state(m), ARITH_STATE_COMPUTE);
    CHECK_EQ(regs(m).alu_phase, ARITH_ALU_NEGATE);

    bool sawAdd = false;
    for (int guard = 0; guard < 200 && arith_machine_state(m) == ARITH_STATE_COMPUTE; ++guard) {
        arith_machine_step(m);
        if (regs(m).alu_phase == ARITH_ALU_ADD)
            sawAdd = true;
    }
    CHECK(sawAdd);
    CHECK_EQ(arith_machine_state(m), ARITH_STATE_NORMALIZE);
    CHECK_FLOAT_BITS(arith_machine_run(m), 2.f);
}

TEST(subtract_normalize_shifts_one_leading_zero_per_cycle)
{
    Machine m(ARITH_OP_SUB, 1.f, 0.9375f);             // difference 0x080000: 4 leading zeros
    const std::vector<ArithState> t = trace(m);
    CHECK_EQ(count(t, ARITH_STATE_NORMALIZE), 5);      // 4 shifts + the step that moves on
    CHECK_FLOAT_BITS(arith_machine_result(m), 0.0625f);
}

TEST(stepping_after_done_changes_nothing)
{
    Machine m(ARITH_OP_ADD, 1.f, 2.f);
    const float result = arith_machine_run(m);
    const uint32_t cycles = arith_machine_cycles(m);

    CHECK_EQ(arith_machine_step(m), 1);
    CHECK_EQ(arith_machine_step(m), 1);
    CHECK_EQ(arith_machine_state(m), ARITH_STATE_DONE);
    CHECK_EQ(arith_machine_cycles(m), cycles);
    CHECK_FLOAT_BITS(arith_machine_result(m), result);
    CHECK_FLOAT_BITS(arith_machine_run(m), result);
}

TEST(run_finishes_from_any_point)
{
    Machine m(ARITH_OP_MUL, 3.f, 4.f);
    for (int i = 0; i < 7; ++i)
        CHECK_EQ(arith_machine_step(m), 0);
    CHECK_FLOAT_BITS(arith_machine_run(m), 12.f);
    CHECK_EQ(arith_machine_cycles(m), 28u);
}
