#include "pch.h"
#include "arithmetic_machine.h"

#include <bit>
#include <utility>

namespace arith {
namespace {

constexpr uint64_t HiddenBit    = 1ULL << 23;
constexpr uint64_t MantissaMask = 0x7FFFFF;
constexpr int32_t  InfExponent  = 0xFF;
constexpr uint64_t QuietNanBit  = 1ULL << 22;

bool isNegative(int32_t bits)
{
    return (static_cast<uint32_t>(bits) & 0x80000000u) != 0;
}

// The operand's bit pattern without its sign. IEEE-754 orders positive floats
// the same way as their bit patterns, so these compare as magnitudes.
uint32_t magnitudeBits(int32_t bits)
{
    return static_cast<uint32_t>(bits) & 0x7FFFFFFFu;
}

bool isZero(int32_t magnitude)
{
    return magnitude == 0;                    // +0 and -0 both strip to 0
}

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

// Unpack: resolve the signs, then split both operands into exponent and
// mantissa.
//
// Subtracting is adding the negated operand, and adding two numbers of unlike
// sign is a magnitude subtraction, so each requested operation reduces to one
// of four operations on magnitudes plus a result sign. For the additive ones
// the operands are ordered so that |a| >= |b|: that way Align only ever has
// to shift b right, and the result takes the sign of a.
void ArithmeticMachine::unpack()
{
    r_.signA = isNegative(r_.bitsA);
    r_.signB = isNegative(r_.bitsB);
    if (op_ == Op::Sub)
        r_.signB = !r_.signB;                 // a - b == a + (-b)

    // From here on bitsA/bitsB hold magnitudes and the signs travel separately.
    r_.bitsA = static_cast<int32_t>(magnitudeBits(r_.bitsA));
    r_.bitsB = static_cast<int32_t>(magnitudeBits(r_.bitsB));

    switch (op_) {
    case Op::Add:
    case Op::Sub:
        r_.magOp = (r_.signA == r_.signB) ? MagOp::Add : MagOp::Sub;
        if (r_.bitsB > r_.bitsA) {
            std::swap(r_.bitsA, r_.bitsB);
            std::swap(r_.signA, r_.signB);
        }
        r_.negative = r_.signA;
        break;

    case Op::Mul:
    case Op::Div:
        r_.magOp = (op_ == Op::Mul) ? MagOp::Mul : MagOp::Div;
        r_.negative = (r_.signA != r_.signB);
        break;
    }

    if (resolveTrivialResult()) {
        state_ = State::Pack;
        return;
    }

    r_.expA = exponentOf(r_.bitsA);
    r_.expB = exponentOf(r_.bitsB);
    r_.manA = mantissaOf(r_.bitsA);
    r_.manB = mantissaOf(r_.bitsB);
    state_ = State::Align;
}

// Cases the ALU cannot be used for, because a zero operand has no hidden bit
// to work with and a zero divisor has no quotient. Fills in the result fields
// Pack reads and returns true when it has handled the operands.
bool ArithmeticMachine::resolveTrivialResult()
{
    const bool zeroA = isZero(r_.bitsA);
    const bool zeroB = isZero(r_.bitsB);

    // Copies an operand through unchanged: Pack masks the mantissa to 23 bits
    // and re-attaches the exponent, so this reproduces its bit pattern exactly.
    const auto passThrough = [this](int32_t bits, bool sign) {
        r_.exponent = exponentOf(bits);
        r_.mantissa = static_cast<uint64_t>(bits) & MantissaMask;
        r_.negative = sign;
        return true;
    };
    const auto zero = [this](bool sign) {
        r_.exponent = 0;
        r_.mantissa = 0;
        r_.negative = sign;
        return true;
    };

    switch (r_.magOp) {
    case MagOp::Add:
        // |a| >= |b|, and both operands have the same sign, so a zero b means
        // the result is a (and if a is zero too, it is a signed zero already).
        if (zeroB)
            return passThrough(r_.bitsA, r_.signA);
        return false;

    case MagOp::Sub:
        // Equal magnitudes of unlike sign cancel. IEEE-754 gives +0 for this,
        // whichever way round the operands were.
        if (r_.bitsA == r_.bitsB)
            return zero(false);
        if (zeroB)                            // |a| > |b| == 0
            return passThrough(r_.bitsA, r_.signA);
        return false;

    case MagOp::Mul:
        if (zeroA || zeroB)
            return zero(r_.negative);
        return false;

    case MagOp::Div:
        if (zeroB) {                          // x / 0
            r_.exponent = InfExponent;
            r_.mantissa = zeroA ? QuietNanBit : 0;   // 0 / 0 is undefined -> NaN
            r_.negative = zeroA ? false : r_.negative;
            return true;
        }
        if (zeroA)
            return zero(r_.negative);
        return false;
    }
    return false;
}

// Align: bring the mantissas to a common scale, work out the result exponent
// and hand the mantissas to the ALU.
void ArithmeticMachine::align()
{
    switch (r_.magOp) {
    case MagOp::Add:
    case MagOp::Sub:
        // Unpack ordered the operands, so expA >= expB.
        r_.manB = shiftRight(r_.manB, r_.expA - r_.expB);
        r_.exponent = r_.expA;
        alu_.begin(r_.magOp == MagOp::Add ? BitSerialAlu::Op::Add : BitSerialAlu::Op::Sub,
                   r_.manA, r_.manB);
        break;

    case MagOp::Mul:
        r_.exponent = r_.expA + r_.expB - Bias;
        alu_.begin(BitSerialAlu::Op::Mul, r_.manA, r_.manB);
        break;

    case MagOp::Div:
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

    switch (r_.magOp) {
    case MagOp::Add:
        if (m & (1ULL << 24)) {              // carry out of the mantissa
            m >>= 1;
            ++r_.exponent;
        }
        state_ = State::Pack;
        break;

    case MagOp::Sub:
        // Equal magnitudes never get here (Unpack handles them), so the
        // difference is non-zero and only needs its leading one shifted back.
        if ((m & HiddenBit) == 0) {          // one leading-zero shift per cycle
            m <<= 1;
            --r_.exponent;
        } else {
            state_ = State::Pack;
        }
        break;

    case MagOp::Mul:
        if (m & (1ULL << 47)) {              // product in [2, 4): drop 24 bits
            m >>= 24;
            ++r_.exponent;
        } else {                             // product in [1, 2): drop 23 bits
            m >>= 23;
        }
        state_ = State::Pack;
        break;

    case MagOp::Div:
        if (m < HiddenBit) {                 // quotient in [0.5, 1)
            m <<= 1;
            --r_.exponent;
        }
        state_ = State::Pack;
        break;
    }
}

// Pack: sign | exponent | fraction.
//
// The exponent is masked to its 8 bits: an exponent that overflowed (the
// algorithm has no overflow detection) then wraps instead of spilling into
// the sign bit and silently negating the result.
void ArithmeticMachine::pack()
{
    uint32_t bits = ((static_cast<uint32_t>(r_.exponent) & 0xFFu) << 23)
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

const char* toString(MagOp op)
{
    switch (op) {
    case MagOp::Add: return "|a|+|b|";
    case MagOp::Sub: return "|a|-|b|";
    case MagOp::Mul: return "|a|*|b|";
    case MagOp::Div: return "|a|/|b|";
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
