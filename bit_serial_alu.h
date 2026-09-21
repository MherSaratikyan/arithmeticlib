// bit_serial_alu.h : the integer primitives of the reference module
// (AddBits, SubBits, MulBits, DivBits) as a clocked state machine.
//
// begin() loads the operands; every step() executes exactly one iteration of
// the corresponding loop, so a caller can clock the ALU and watch it work.
#pragma once

#include <cstdint>

namespace arith {

class BitSerialAlu {
public:
    enum class Op    { Add, Sub, Mul, Div };
    enum class Phase { Idle, Add, Negate, Mul, Div, Done };

    void begin(Op op, uint64_t a, uint64_t b);
    bool step();                                   // true once the result is ready

    Phase    phase()     const { return phase_; }
    bool     done()      const { return phase_ == Phase::Done; }
    uint64_t result()    const { return result_; }
    uint32_t cycles()    const { return cycles_; }
    uint64_t a()         const { return a_; }
    uint64_t b()         const { return b_; }
    uint64_t remainder() const { return remainder_; }
    int      bit()       const { return bit_; }

    // Run-to-completion versions, used as the "combinational adder" inside
    // the multiply and divide steps.
    static uint64_t addBits(uint64_t a, uint64_t b);
    static uint64_t subBits(uint64_t a, uint64_t b);

private:
    void stepAdd();
    void stepNegate();
    void stepMul();
    void stepDiv();
    void finish(uint64_t value);

    Phase    phase_     = Phase::Idle;
    uint64_t a_         = 0;   // Add/Negate: running sum.  Mul: shifted multiplicand.  Div: dividend
    uint64_t b_         = 0;   // Add/Negate: pending carry. Mul: remaining multiplier.  Div: divisor
    uint64_t minuend_   = 0;   // Sub: operand a, kept aside while b is being negated
    uint64_t result_    = 0;   // Mul: partial product. Div: quotient. The answer once Done
    uint64_t remainder_ = 0;   // Div: partial remainder
    int      bit_       = 0;   // Div: index of the dividend bit being brought down (63..0)
    uint32_t cycles_    = 0;
};

} // namespace arith
