// arithmetic_machine.h : the four floating-point operations of the reference
// module (Add, Subtract, Multiply, Divide) as one state machine, extended to
// signed operands.
//
// Signs are resolved in Unpack, which reduces the requested operation to an
// operation on magnitudes (MagOp) and a result sign; everything after that
// works on positive magnitudes, exactly like the unsigned reference:
//
//     Unpack --> Align --> Compute --> Normalize --> Pack --> Done
//        |                  ^   |        ^   |         ^
//        |                  +---+        +---+         |
//        |            (one ALU cycle)  (one shift)     |
//        +-------------------------------------------- +
//          trivial result (a zero operand, cancellation, division by zero)
#pragma once

#include <cstdint>
#include "bit_serial_alu.h"

namespace arith {

enum class Op    { Add, Sub, Mul, Div };   // the operation that was requested
enum class MagOp { Add, Sub, Mul, Div };   // what that becomes on the magnitudes
enum class State { Unpack, Align, Compute, Normalize, Pack, Done };

struct Registers {
    int32_t  bitsA = 0, bitsB = 0;     // operand magnitudes, sign stripped
                                       //   (additive ops order them |a| >= |b|)
    bool     signA = false;            // operand signs; signB is flipped for Subtract,
    bool     signB = false;            //   because a - b == a + (-b)
    int32_t  expA  = 0, expB  = 0;     // biased exponents
    uint64_t manA  = 0, manB  = 0;     // mantissas with the hidden bit
    MagOp    magOp = MagOp::Add;       // operation carried out on the magnitudes
    uint64_t mantissa   = 0;           // result mantissa (Normalize / Pack)
    int32_t  exponent   = 0;           // result exponent, biased
    bool     negative   = false;       // result sign
    uint32_t resultBits = 0;           // packed result
};

class ArithmeticMachine {
public:
    static constexpr int32_t Bias = 127;

    ArithmeticMachine(Op op, float a, float b);
    void reset(Op op, float a, float b);

    bool  step();                      // execute the current state; true once Done
    float run();                       // step until Done

    Op       op()     const { return op_; }
    State    state()  const { return state_; }
    bool     done()   const { return state_ == State::Done; }
    float    result() const;           // 0.0f until Done
    uint32_t cycles() const { return cycles_; }

    const Registers&    regs() const { return r_; }
    const BitSerialAlu& alu()  const { return alu_; }

private:
    void unpack();
    void align();
    void compute();
    void normalize();
    void pack();

    // Called from Unpack. Returns true when the result follows from the
    // operands alone (a zero operand, exact cancellation, division by zero),
    // in which case it fills in exponent/mantissa/negative for Pack.
    bool resolveTrivialResult();

    Op           op_;
    State        state_  = State::Unpack;
    Registers    r_;
    BitSerialAlu alu_;
    uint32_t     cycles_ = 0;
};

const char* toString(Op op);
const char* toString(MagOp op);
const char* toString(State s);
const char* toString(BitSerialAlu::Phase p);

} // namespace arith
