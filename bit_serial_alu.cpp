#include "pch.h"
#include "bit_serial_alu.h"

namespace arith {

uint64_t BitSerialAlu::addBits(uint64_t a, uint64_t b)
{
    while (b != 0) {
        uint64_t carry = (a & b) << 1;
        a = a ^ b;
        b = carry;
    }
    return a;
}

uint64_t BitSerialAlu::subBits(uint64_t a, uint64_t b)
{
    return addBits(a, addBits(~b, 1));
}

void BitSerialAlu::begin(Op op, uint64_t a, uint64_t b)
{
    result_    = 0;
    remainder_ = 0;
    bit_       = 0;
    cycles_    = 0;

    switch (op) {
    case Op::Add:
        a_ = a; b_ = b;
        phase_ = Phase::Add;
        break;
    case Op::Sub:
        // SubBits(a, b) = AddBits(a, AddBits(~b, 1)): negate first, then add.
        minuend_ = a;
        a_ = ~b; b_ = 1;
        phase_ = Phase::Negate;
        break;
    case Op::Mul:
        a_ = a; b_ = b;
        phase_ = Phase::Mul;
        break;
    case Op::Div:
        a_ = a; b_ = b;
        bit_ = 63;
        phase_ = Phase::Div;
        break;
    }
}

bool BitSerialAlu::step()
{
    switch (phase_) {
    case Phase::Add:    stepAdd();    break;
    case Phase::Negate: stepNegate(); break;
    case Phase::Mul:    stepMul();    break;
    case Phase::Div:    stepDiv();    break;
    case Phase::Idle:
    case Phase::Done:   break;
    }
    return done();
}

// One ripple of the carry chain: a ^ b keeps the sum bits, (a & b) << 1 is the carry.
void BitSerialAlu::stepAdd()
{
    uint64_t carry = (a_ & b_) << 1;
    a_ = a_ ^ b_;
    b_ = carry;
    ++cycles_;
    if (b_ == 0)
        finish(a_);
}

// Same ripple computing ~b + 1; when it settles, switch to the real addition.
void BitSerialAlu::stepNegate()
{
    uint64_t carry = (a_ & b_) << 1;
    a_ = a_ ^ b_;
    b_ = carry;
    ++cycles_;
    if (b_ == 0) {
        b_ = a_;            // -b
        a_ = minuend_;
        phase_ = Phase::Add;
    }
}

// Shift-and-add: consume one multiplier bit per cycle.
void BitSerialAlu::stepMul()
{
    if ((b_ & 1) == 1)
        result_ = addBits(result_, a_);
    a_ <<= 1;
    b_ >>= 1;
    ++cycles_;
    if (b_ == 0)
        finish(result_);
}

// Restoring division: bring down one dividend bit per cycle, from bit 63 to bit 0.
void BitSerialAlu::stepDiv()
{
    remainder_ <<= 1;
    remainder_ |= (a_ >> bit_) & 1;
    if (remainder_ >= b_) {
        remainder_ = subBits(remainder_, b_);
        result_ |= 1ULL << bit_;
    }
    ++cycles_;
    if (bit_ == 0)
        finish(result_);
    else
        --bit_;
}

void BitSerialAlu::finish(uint64_t value)
{
    result_ = value;
    phase_  = Phase::Done;
}

} // namespace arith
