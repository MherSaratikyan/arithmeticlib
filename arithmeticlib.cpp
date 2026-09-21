// arithmeticlib.cpp : exported C API. Thin wrapper over arith::ArithmeticMachine.
#include "pch.h"
#include "arithmeticlib.h"
#include "arithmetic_machine.h"

#include <new>

using namespace arith;

// The opaque handle handed out to C callers.
struct ArithMachine {
    ArithMachine(Op op, float a, float b) : impl(op, a, b) {}
    ArithmeticMachine impl;
};

namespace {

// The public enums mirror the internal ones one-to-one.
static_assert(static_cast<int>(State::Unpack)    == ARITH_STATE_UNPACK);
static_assert(static_cast<int>(State::Align)     == ARITH_STATE_ALIGN);
static_assert(static_cast<int>(State::Compute)   == ARITH_STATE_COMPUTE);
static_assert(static_cast<int>(State::Normalize) == ARITH_STATE_NORMALIZE);
static_assert(static_cast<int>(State::Pack)      == ARITH_STATE_PACK);
static_assert(static_cast<int>(State::Done)      == ARITH_STATE_DONE);

static_assert(static_cast<int>(BitSerialAlu::Phase::Idle)   == ARITH_ALU_IDLE);
static_assert(static_cast<int>(BitSerialAlu::Phase::Add)    == ARITH_ALU_ADD);
static_assert(static_cast<int>(BitSerialAlu::Phase::Negate) == ARITH_ALU_NEGATE);
static_assert(static_cast<int>(BitSerialAlu::Phase::Mul)    == ARITH_ALU_MUL);
static_assert(static_cast<int>(BitSerialAlu::Phase::Div)    == ARITH_ALU_DIV);
static_assert(static_cast<int>(BitSerialAlu::Phase::Done)   == ARITH_ALU_DONE);

Op toOp(ArithOp op)
{
    switch (op) {
    case ARITH_OP_SUB: return Op::Sub;
    case ARITH_OP_MUL: return Op::Mul;
    case ARITH_OP_DIV: return Op::Div;
    case ARITH_OP_ADD:
    default:           return Op::Add;
    }
}

uint64_t runAlu(BitSerialAlu::Op op, uint64_t a, uint64_t b)
{
    BitSerialAlu alu;
    alu.begin(op, a, b);
    while (!alu.step()) {}
    return alu.result();
}

} // namespace

extern "C" {

/* --- One-shot operations ------------------------------------------------- */

float arith_add(float a, float b)      { return ArithmeticMachine(Op::Add, a, b).run(); }
float arith_subtract(float a, float b) { return ArithmeticMachine(Op::Sub, a, b).run(); }
float arith_multiply(float a, float b) { return ArithmeticMachine(Op::Mul, a, b).run(); }
float arith_divide(float a, float b)   { return ArithmeticMachine(Op::Div, a, b).run(); }

float arith_compute(ArithOp op, float a, float b)
{
    return ArithmeticMachine(toOp(op), a, b).run();
}

/* --- Integer primitives -------------------------------------------------- */

uint64_t arith_add_bits(uint64_t a, uint64_t b) { return runAlu(BitSerialAlu::Op::Add, a, b); }
uint64_t arith_sub_bits(uint64_t a, uint64_t b) { return runAlu(BitSerialAlu::Op::Sub, a, b); }
uint64_t arith_mul_bits(uint64_t a, uint64_t b) { return runAlu(BitSerialAlu::Op::Mul, a, b); }
uint64_t arith_div_bits(uint64_t a, uint64_t b) { return runAlu(BitSerialAlu::Op::Div, a, b); }

/* --- Stepwise state machine ---------------------------------------------- */

ArithMachine* arith_machine_create(ArithOp op, float a, float b)
{
    return new (std::nothrow) ArithMachine(toOp(op), a, b);
}

void arith_machine_destroy(ArithMachine* m)
{
    delete m;
}

void arith_machine_reset(ArithMachine* m, ArithOp op, float a, float b)
{
    if (m)
        m->impl.reset(toOp(op), a, b);
}

int arith_machine_step(ArithMachine* m)
{
    return m ? (m->impl.step() ? 1 : 0) : 1;
}

float arith_machine_run(ArithMachine* m)
{
    return m ? m->impl.run() : 0.0f;
}

ArithState arith_machine_state(const ArithMachine* m)
{
    return m ? static_cast<ArithState>(m->impl.state()) : ARITH_STATE_DONE;
}

int arith_machine_is_done(const ArithMachine* m)
{
    return (!m || m->impl.done()) ? 1 : 0;
}

float arith_machine_result(const ArithMachine* m)
{
    return m ? m->impl.result() : 0.0f;
}

uint32_t arith_machine_cycles(const ArithMachine* m)
{
    return m ? m->impl.cycles() : 0;
}

void arith_machine_registers(const ArithMachine* m, ArithRegisters* out)
{
    if (!m || !out)
        return;

    const Registers&    r   = m->impl.regs();
    const BitSerialAlu& alu = m->impl.alu();

    out->bits_a      = r.bitsA;
    out->bits_b      = r.bitsB;
    out->exp_a       = r.expA;
    out->exp_b       = r.expB;
    out->man_a       = r.manA;
    out->man_b       = r.manB;
    out->mantissa    = r.mantissa;
    out->exponent    = r.exponent;
    out->negative    = r.negative ? 1 : 0;
    out->result_bits = r.resultBits;
    out->alu_phase   = static_cast<ArithAluPhase>(alu.phase());
    out->alu_a       = alu.a();
    out->alu_b       = alu.b();
    out->alu_acc     = alu.result();
    out->alu_rem     = alu.remainder();
    out->alu_bit     = alu.bit();
    out->alu_cycles  = alu.cycles();
}

/* --- Names --------------------------------------------------------------- */

const char* arith_op_name(ArithOp op)
{
    if (op < ARITH_OP_ADD || op > ARITH_OP_DIV)
        return "?";
    return toString(toOp(op));
}

const char* arith_state_name(ArithState s)
{
    if (s < ARITH_STATE_UNPACK || s > ARITH_STATE_DONE)
        return "?";
    return toString(static_cast<State>(s));
}

const char* arith_alu_phase_name(ArithAluPhase p)
{
    if (p < ARITH_ALU_IDLE || p > ARITH_ALU_DONE)
        return "?";
    return toString(static_cast<BitSerialAlu::Phase>(p));
}

} // extern "C"
