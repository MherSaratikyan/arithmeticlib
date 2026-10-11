// arithmeticlib.cpp : exported C API. Thin wrapper over arith::ArithmeticMachine.
#include "pch.h"
#include "arithmeticlib.h"
#include "arithmetic_machine.h"

#include <new>

#ifdef _OPENMP
#  include <omp.h>
#endif

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

static_assert(static_cast<int>(MagOp::Add) == ARITH_MAG_ADD);
static_assert(static_cast<int>(MagOp::Sub) == ARITH_MAG_SUB);
static_assert(static_cast<int>(MagOp::Mul) == ARITH_MAG_MUL);
static_assert(static_cast<int>(MagOp::Div) == ARITH_MAG_DIV);

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

BitSerialAlu::Op toAluOp(ArithOp op)
{
    switch (op) {
    case ARITH_OP_SUB: return BitSerialAlu::Op::Sub;
    case ARITH_OP_MUL: return BitSerialAlu::Op::Mul;
    case ARITH_OP_DIV: return BitSerialAlu::Op::Div;
    case ARITH_OP_ADD:
    default:           return BitSerialAlu::Op::Add;
    }
}

uint64_t runAlu(BitSerialAlu::Op op, uint64_t a, uint64_t b)
{
    BitSerialAlu alu;
    alu.begin(op, a, b);
    while (!alu.step()) {}
    return alu.result();
}

// Below this many elements, handing work to other threads costs more than
// doing it here. One float operation is a few dozen ALU cycles, so the
// cross-over sits in the low thousands.
constexpr size_t kParallelThreshold = 2048;

// Runs body(i) for i in [0, n), split across threads once the batch is big
// enough to pay for them. Every element is written by exactly one thread and
// reads only its own inputs, so no synchronisation is needed and the result
// does not depend on the schedule.
//
// The index is signed because MSVC's /openmp implements OpenMP 2.0, which
// requires a signed loop counter.
template <class Body>
void parallelFor(size_t n, Body body)
{
    const ptrdiff_t count = static_cast<ptrdiff_t>(n);
#pragma omp parallel for schedule(static) \
        if (count >= static_cast<ptrdiff_t>(kParallelThreshold))
    for (ptrdiff_t i = 0; i < count; ++i)
        body(static_cast<size_t>(i));
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

uint64_t arith_mul_bits_parallel(uint64_t a, uint64_t b)
{
    return BitSerialAlu::mulBitsParallel(a, b);
}

/* --- Batch operations ---------------------------------------------------- */

void arith_compute_array(ArithOp op, const float* a, const float* b, float* out, size_t n)
{
    if (!a || !b || !out)
        return;
    const Op internal = toOp(op);
    parallelFor(n, [=](size_t i) {
        out[i] = ArithmeticMachine(internal, a[i], b[i]).run();
    });
}

void arith_compute_array_scalar(ArithOp op, const float* a, float b, float* out, size_t n)
{
    if (!a || !out)
        return;
    const Op internal = toOp(op);
    parallelFor(n, [=](size_t i) {
        out[i] = ArithmeticMachine(internal, a[i], b).run();
    });
}

void arith_bits_array(ArithOp op, const uint64_t* a, const uint64_t* b, uint64_t* out, size_t n)
{
    if (!a || !b || !out)
        return;
    const BitSerialAlu::Op aluOp = toAluOp(op);
    parallelFor(n, [=](size_t i) {
        out[i] = runAlu(aluOp, a[i], b[i]);
    });
}

/* --- Parallel runtime ---------------------------------------------------- */

int arith_parallel_available(void)
{
#ifdef _OPENMP
    return 1;
#else
    return 0;
#endif
}

int arith_parallel_threads(void)
{
#ifdef _OPENMP
    return omp_get_max_threads();
#else
    return 1;
#endif
}

void arith_parallel_set_threads(int n)
{
#ifdef _OPENMP
    omp_set_num_threads(n > 0 ? n : omp_get_num_procs());
#else
    (void)n;
#endif
}

size_t arith_parallel_threshold(void)
{
    return kParallelThreshold;
}

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
    out->sign_a      = r.signA ? 1 : 0;
    out->sign_b      = r.signB ? 1 : 0;
    out->exp_a       = r.expA;
    out->exp_b       = r.expB;
    out->man_a       = r.manA;
    out->man_b       = r.manB;
    out->mag_op      = static_cast<ArithMagOp>(r.magOp);
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

const char* arith_mag_op_name(ArithMagOp op)
{
    if (op < ARITH_MAG_ADD || op > ARITH_MAG_DIV)
        return "?";
    return toString(static_cast<MagOp>(op));
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
