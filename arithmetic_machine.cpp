#include "pch.h"
#include "arithmetic_machine.h"

#include <bit>
#include <utility>

namespace arith {
namespace {

constexpr uint64_t HiddenBit    = 1ULL << 23;
constexpr uint64_t MantissaMask = 0x7FFFFF;

int32_t exponentOf(int32_t bits)
{
    return static_cast<int32_t>((static_cast<uint32_t>(bits) >> 23) & 0xFF);
}

uint64_t mantissaOf(int32_t bits)
{
    return static_cast<uint64_t>(bits & 0x7FFFFF) | HiddenBit;
}

// Right shift with a bounded count. Shifting a 64-bit value by 64 or more is
// undefined in C++ (the reference C# silently masks the count to 6 bits, so
// `x >> 70` would become `x >> 6`). An operand that is 64+ binades smaller
// contributes nothing to the sum, so it is flushed to zero here instead.
// Negative counts can only arise for inputs the algorithm does not support
// (negative floats); they are treated as "no shift".
uint64_t shiftRight(uint64_t v, int32_t n)
{
    if (n <= 0)  return v;
    if (n >= 64) return 0;
    return v >> n;
}

} // namespace

ArithmeticMachine::ArithmeticMachine(Op op, float a, float b)
    : op_(op)
{
    reset(op, a, b);
}

void ArithmeticMachine::reset(Op op, float a, float b)
{
    op_     = op;
    state_  = State::Unpack;
    r_      = Registers{};
    alu_    = BitSerialAlu{};
    cycles_ = 0;
    r_.bitsA = std::bit_cast<int32_t>(a);
    r_.bitsB = std::bit_cast<int32_t>(b);
}

bool ArithmeticMachine::step()
{
    switch (state_) {
    case State::Unpack:    unpack();    break;
    case State::Align:     align();     break;
    case State::Compute:   compute();   break;
    case State::Normalize: normalize(); break;
    case State::Pack:      pack();      break;
    case State::Done:      return true;
    }
    ++cycles_;
    return done();
}

float ArithmeticMachine::run()
{
    while (!step()) {}
    return result();
}

float ArithmeticMachine::result() const
{
    return done() ? std::bit_cast<float>(r_.resultBits) : 0.0f;
}

// Unpack: split both operands into exponent and mantissa. Subtract first
// orders them so the larger bit pattern is the minuend and remembers the sign.
void ArithmeticMachine::unpack()
{
    if (op_ == Op::Sub && r_.bitsB > r_.bitsA) {
        std::swap(r_.bitsA, r_.bitsB);
        r_.negative = true;
    }
    r_.expA = exponentOf(r_.bitsA);
    r_.expB = exponentOf(r_.bitsB);
    r_.manA = mantissaOf(r_.bitsA);
    r_.manB = mantissaOf(r_.bitsB);
    state_ = State::Align;
}

// Align: bring the mantissas to a common scale, work out the result exponent
// and hand the mantissas to the ALU.
void ArithmeticMachine::align()
{
    switch (op_) {
    case Op::Add:
        if (r_.expA > r_.expB) {
            r_.manB = shiftRight(r_.manB, r_.expA - r_.expB);
            r_.expB = r_.expA;
        } else if (r_.expB > r_.expA) {
            r_.manA = shiftRight(r_.manA, r_.expB - r_.expA);
            r_.expA = r_.expB;
        }
        r_.exponent = r_.expA;
        alu_.begin(BitSerialAlu::Op::Add, r_.manA, r_.manB);
        break;

    case Op::Sub:
        r_.manB = shiftRight(r_.manB, r_.expA - r_.expB);
        r_.exponent = r_.expA;
        alu_.begin(BitSerialAlu::Op::Sub, r_.manA, r_.manB);
        break;

    case Op::Mul:
        r_.exponent = r_.expA + r_.expB - Bias;
        alu_.begin(BitSerialAlu::Op::Mul, r_.manA, r_.manB);
        break;

    case Op::Div:
        r_.manA <<= 23;                       // 23 extra fraction bits for the quotient
        r_.exponent = r_.expA - r_.expB + Bias;
        alu_.begin(BitSerialAlu::Op::Div, r_.manA, r_.manB);
        break;
    }
    state_ = State::Compute;
}

// Compute: clock the bit-serial ALU once; move on when it has settled.
void ArithmeticMachine::compute()
{
    if (alu_.step()) {
        r_.mantissa = alu_.result();
        state_ = State::Normalize;
    }
}

// Normalize: put the leading one back at bit 23 and fix the exponent.
void ArithmeticMachine::normalize()
{
    uint64_t& m = r_.mantissa;

    switch (op_) {
    case Op::Add:
        if (m & (1ULL << 24)) {              // carry out of the mantissa
            m >>= 1;
            ++r_.exponent;
        }
        state_ = State::Pack;
        break;

    case Op::Sub:
        if (m == 0) {                        // a == b  ->  +0
            r_.negative = false;
            r_.exponent = 0;
            state_ = State::Pack;
        } else if ((m & HiddenBit) == 0) {   // one leading-zero shift per cycle
            m <<= 1;
            --r_.exponent;
        } else {
            state_ = State::Pack;
        }
        break;

    case Op::Mul:
        if (m & (1ULL << 47)) {              // product in [2, 4): drop 24 bits
            m >>= 24;
            ++r_.exponent;
        } else {                             // product in [1, 2): drop 23 bits
            m >>= 23;
        }
        state_ = State::Pack;
        break;

    case Op::Div:
        if (m < HiddenBit) {                 // quotient in [0.5, 1)
            m <<= 1;
            --r_.exponent;
        }
        state_ = State::Pack;
        break;
    }
}

// Pack: sign | exponent | fraction.
void ArithmeticMachine::pack()
{
    uint32_t bits = (static_cast<uint32_t>(r_.exponent) << 23)
                  | static_cast<uint32_t>(r_.mantissa & MantissaMask);
    if (r_.negative)
        bits |= 0x80000000u;
    r_.resultBits = bits;
    state_ = State::Done;
}

const char* toString(Op op)
{
    switch (op) {
    case Op::Add: return "Add";
    case Op::Sub: return "Subtract";
    case Op::Mul: return "Multiply";
    case Op::Div: return "Divide";
    }
    return "?";
}

const char* toString(State s)
{
    switch (s) {
    case State::Unpack:    return "Unpack";
    case State::Align:     return "Align";
    case State::Compute:   return "Compute";
    case State::Normalize: return "Normalize";
    case State::Pack:      return "Pack";
    case State::Done:      return "Done";
    }
    return "?";
}

const char* toString(BitSerialAlu::Phase p)
{
    switch (p) {
    case BitSerialAlu::Phase::Idle:   return "Idle";
    case BitSerialAlu::Phase::Add:    return "Add";
    case BitSerialAlu::Phase::Negate: return "Negate";
    case BitSerialAlu::Phase::Mul:    return "Mul";
    case BitSerialAlu::Phase::Div:    return "Div";
    case BitSerialAlu::Phase::Done:   return "Done";
    }
    return "?";
}

} // namespace arith
