// test_util.h : helpers shared by the test files.
#pragma once

#include <bit>
#include <cstdint>
#include <vector>

#include <arithmeticlib.h>

namespace testutil {

// xorshift64: small, deterministic, good enough for randomized comparisons.
struct Rng {
    uint64_t state;

    explicit Rng(uint64_t seed) : state(seed ? seed : 0x9E3779B97F4A7C15ull) {}

    uint64_t next()
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }

    // Uniform in [lo, hi].
    int32_t between(int32_t lo, int32_t hi)
    {
        return lo + static_cast<int32_t>(next() % static_cast<uint64_t>(hi - lo + 1));
    }
};

inline float    fromBits(uint32_t bits) { return std::bit_cast<float>(bits); }
inline uint32_t toBits(float f)         { return std::bit_cast<uint32_t>(f); }

// Positive normal float with the given biased exponent and 23-bit fraction.
inline float makeFloat(int32_t biasedExponent, uint32_t fraction)
{
    return fromBits((static_cast<uint32_t>(biasedExponent) << 23) | (fraction & 0x7FFFFFu));
}

inline float randomNormal(Rng& rng, int32_t expLo, int32_t expHi)
{
    return makeFloat(rng.between(expLo, expHi), static_cast<uint32_t>(rng.next()));
}

// Same, with a random sign.
inline float randomSigned(Rng& rng, int32_t expLo, int32_t expHi)
{
    const float magnitude = randomNormal(rng, expLo, expHi);
    return (rng.next() & 1) ? -magnitude : magnitude;
}

// RAII wrapper around an ArithMachine so a failing CHECK never leaks one.
struct Machine {
    ArithMachine* m;
    Machine(ArithOp op, float a, float b) : m(arith_machine_create(op, a, b)) {}
    ~Machine() { arith_machine_destroy(m); }
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;
    operator ArithMachine*() const { return m; }
};

// Steps a machine to completion, recording the state *before* each step.
inline std::vector<ArithState> trace(ArithMachine* m)
{
    std::vector<ArithState> states;
    while (!arith_machine_is_done(m)) {
        states.push_back(arith_machine_state(m));
        arith_machine_step(m);
    }
    return states;
}

inline int count(const std::vector<ArithState>& states, ArithState s)
{
    int n = 0;
    for (ArithState x : states)
        if (x == s) ++n;
    return n;
}

inline ArithRegisters regs(ArithMachine* m)
{
    ArithRegisters r{};
    arith_machine_registers(m, &r);
    return r;
}

} // namespace testutil
