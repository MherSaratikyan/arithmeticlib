// Tests for register snapshots, reset, diagnostic names and null handling.
#include "test_framework.h"
#include "test_util.h"

#include <arithmeticlib.h>
#include <cstring>

using testutil::Machine;
using testutil::regs;
using testutil::toBits;

TEST(registers_expose_decoded_operands_and_alu_state)
{
    Machine m(ARITH_OP_MUL, 3.f, 4.f);
    arith_machine_step(m);                             // Unpack
    ArithRegisters r = regs(m);
    CHECK_EQ(r.exp_a, 128);
    CHECK_EQ(r.man_a, 0xC00000ull);
    CHECK_EQ(r.exp_b, 129);
    CHECK_EQ(r.man_b, 0x800000ull);
    CHECK_EQ(r.negative, 0);

    arith_machine_step(m);                             // Align
    r = regs(m);
    CHECK_EQ(r.exponent, 130);                         // 128 + 129 - 127
    CHECK_EQ(r.alu_phase, ARITH_ALU_MUL);
    CHECK_EQ(r.alu_a, 0xC00000ull);
    CHECK_EQ(r.alu_b, 0x800000ull);
    CHECK_EQ(r.alu_cycles, 0u);

    arith_machine_run(m);
    r = regs(m);
    CHECK_EQ(r.alu_phase, ARITH_ALU_DONE);
    CHECK_EQ(r.alu_cycles, 24u);
    CHECK_EQ(r.mantissa, 0xC00000ull);                 // 12 = 1.5 * 2^3
    CHECK_EQ(r.exponent, 130);
    CHECK_EQ(r.result_bits, toBits(12.f));
}

TEST(subtract_swaps_operands_and_records_the_sign)
{
    Machine m(ARITH_OP_SUB, 1.f, 3.f);
    arith_machine_step(m);                             // Unpack
    const ArithRegisters r = regs(m);
    CHECK_EQ(r.bits_a, static_cast<int32_t>(toBits(3.f)));
    CHECK_EQ(r.bits_b, static_cast<int32_t>(toBits(1.f)));
    CHECK_EQ(r.negative, 1);
    CHECK_FLOAT_BITS(arith_machine_run(m), -2.f);
}

TEST(divide_registers_show_the_quotient_being_built)
{
    Machine m(ARITH_OP_DIV, 1.f, 2.f);
    arith_machine_step(m);
    arith_machine_step(m);                             // Align: dividend pre-shifted by 23
    ArithRegisters r = regs(m);
    CHECK_EQ(r.alu_phase, ARITH_ALU_DIV);
    CHECK_EQ(r.alu_a, 0x800000ull << 23);
    CHECK_EQ(r.alu_b, 0x800000ull);
    CHECK_EQ(r.alu_bit, 63);

    for (int i = 0; i < 10; ++i)
        arith_machine_step(m);
    r = regs(m);
    CHECK_EQ(r.alu_bit, 53);
    CHECK_EQ(r.alu_cycles, 10u);
    CHECK_EQ(r.alu_acc, 0ull);                         // no quotient bits yet: 2^46 / 2^23 = 2^23

    CHECK_FLOAT_BITS(arith_machine_run(m), 0.5f);
    CHECK_EQ(regs(m).alu_acc, 0x800000ull);
}

TEST(reset_reuses_a_machine)
{
    Machine m(ARITH_OP_ADD, 1.f, 2.f);
    CHECK_FLOAT_BITS(arith_machine_run(m), 3.f);

    arith_machine_reset(m, ARITH_OP_MUL, 2.f, 5.f);
    CHECK_EQ(arith_machine_state(m), ARITH_STATE_UNPACK);
    CHECK_EQ(arith_machine_is_done(m), 0);
    CHECK_EQ(arith_machine_cycles(m), 0u);
    CHECK_EQ(regs(m).alu_phase, ARITH_ALU_IDLE);
    CHECK_FLOAT_BITS(arith_machine_run(m), 10.f);
}

TEST(names_are_human_readable)
{
    CHECK(std::strcmp(arith_op_name(ARITH_OP_ADD), "Add") == 0);
    CHECK(std::strcmp(arith_op_name(ARITH_OP_SUB), "Subtract") == 0);
    CHECK(std::strcmp(arith_op_name(ARITH_OP_MUL), "Multiply") == 0);
    CHECK(std::strcmp(arith_op_name(ARITH_OP_DIV), "Divide") == 0);
    CHECK(std::strcmp(arith_op_name(static_cast<ArithOp>(99)), "?") == 0);

    CHECK(std::strcmp(arith_state_name(ARITH_STATE_UNPACK), "Unpack") == 0);
    CHECK(std::strcmp(arith_state_name(ARITH_STATE_COMPUTE), "Compute") == 0);
    CHECK(std::strcmp(arith_state_name(ARITH_STATE_DONE), "Done") == 0);
    CHECK(std::strcmp(arith_state_name(static_cast<ArithState>(-1)), "?") == 0);

    CHECK(std::strcmp(arith_alu_phase_name(ARITH_ALU_NEGATE), "Negate") == 0);
    CHECK(std::strcmp(arith_alu_phase_name(ARITH_ALU_DIV), "Div") == 0);
    CHECK(std::strcmp(arith_alu_phase_name(static_cast<ArithAluPhase>(42)), "?") == 0);
}

TEST(null_handles_are_tolerated)
{
    ArithRegisters r{};
    CHECK_EQ(arith_machine_step(nullptr), 1);
    CHECK_EQ(arith_machine_is_done(nullptr), 1);
    CHECK_EQ(arith_machine_state(nullptr), ARITH_STATE_DONE);
    CHECK_EQ(arith_machine_cycles(nullptr), 0u);
    CHECK_FLOAT_BITS(arith_machine_result(nullptr), 0.f);
    CHECK_FLOAT_BITS(arith_machine_run(nullptr), 0.f);
    arith_machine_registers(nullptr, &r);
    arith_machine_reset(nullptr, ARITH_OP_ADD, 1.f, 1.f);
    arith_machine_destroy(nullptr);

    Machine m(ARITH_OP_ADD, 1.f, 1.f);
    arith_machine_registers(m, nullptr);               // must not crash
    CHECK_EQ(arith_machine_state(m), ARITH_STATE_UNPACK);
}
