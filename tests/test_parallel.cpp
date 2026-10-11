// Tests for the batch API and the OpenMP runtime hooks.
//
// The point of these is that parallelism must not be observable in the
// results: a batch has to be bitwise identical to the same operations run one
// at a time, whatever the thread count and whichever side of the threshold
// the batch size falls on.
#include "test_framework.h"
#include "test_util.h"

#include <arithmeticlib.h>
#include <vector>

using testutil::toBits;

namespace {

const ArithOp kOps[] = { ARITH_OP_ADD, ARITH_OP_SUB, ARITH_OP_MUL, ARITH_OP_DIV };

std::vector<float> randomFloats(testutil::Rng& rng, size_t n)
{
    std::vector<float> v(n);
    for (size_t i = 0; i < n; ++i)
        v[i] = testutil::randomSigned(rng, 100, 150);
    return v;
}

std::vector<uint64_t> randomBits(testutil::Rng& rng, size_t n)
{
    std::vector<uint64_t> v(n);
    for (size_t i = 0; i < n; ++i)
        v[i] = rng.next();
    return v;
}

// Compares a batch result against the single-value functions without
// flooding the log: counts the mismatches, then reports the first one.
void expectMatchesScalar(ArithOp op, const std::vector<float>& a,
                         const std::vector<float>& b, const std::vector<float>& out)
{
    size_t mismatches = 0, first = 0;
    for (size_t i = 0; i < out.size(); ++i) {
        if (toBits(out[i]) != toBits(arith_compute(op, a[i], b[i]))) {
            if (mismatches++ == 0)
                first = i;
        }
    }
    CHECK_EQ(mismatches, static_cast<size_t>(0));
    if (mismatches != 0)
        CHECK_FLOAT_BITS(out[first], arith_compute(op, a[first], b[first]));
}

} // namespace

TEST(a_batch_above_the_threshold_matches_the_scalar_functions)
{
    testutil::Rng rng(41);
    // Comfortably past the threshold and not a multiple of it, so the last
    // chunk handed to a thread is a partial one.
    const size_t n = arith_parallel_threshold() * 4 + 7;
    const std::vector<float> a = randomFloats(rng, n);
    const std::vector<float> b = randomFloats(rng, n);
    std::vector<float> out(n);

    for (ArithOp op : kOps) {
        arith_compute_array(op, a.data(), b.data(), out.data(), n);
        expectMatchesScalar(op, a, b, out);
    }
}

TEST(a_batch_below_the_threshold_matches_the_scalar_functions)
{
    testutil::Rng rng(42);
    const size_t n = 10;                       // runs serially
    const std::vector<float> a = randomFloats(rng, n);
    const std::vector<float> b = randomFloats(rng, n);
    std::vector<float> out(n);

    for (ArithOp op : kOps) {
        arith_compute_array(op, a.data(), b.data(), out.data(), n);
        expectMatchesScalar(op, a, b, out);
    }
}

TEST(a_batch_is_independent_of_the_thread_count)
{
    testutil::Rng rng(43);
    const size_t n = arith_parallel_threshold() * 3;
    const std::vector<float> a = randomFloats(rng, n);
    const std::vector<float> b = randomFloats(rng, n);
    std::vector<float> serial(n), parallel(n);

    const int threads = arith_parallel_threads();

    arith_parallel_set_threads(1);
    for (ArithOp op : kOps) {
        arith_compute_array(op, a.data(), b.data(), serial.data(), n);

        arith_parallel_set_threads(threads > 1 ? threads : 4);
        arith_compute_array(op, a.data(), b.data(), parallel.data(), n);
        arith_parallel_set_threads(1);

        size_t mismatches = 0;
        for (size_t i = 0; i < n; ++i)
            if (toBits(serial[i]) != toBits(parallel[i]))
                ++mismatches;
        CHECK_EQ(mismatches, static_cast<size_t>(0));
    }

    arith_parallel_set_threads(0);             // back to the default
    CHECK(arith_parallel_threads() >= 1);
}

TEST(a_batch_can_write_in_place)
{
    testutil::Rng rng(44);
    const size_t n = arith_parallel_threshold() * 2 + 3;
    const std::vector<float> a = randomFloats(rng, n);
    const std::vector<float> b = randomFloats(rng, n);

    std::vector<float> inPlace = a;            // out aliases a
    arith_compute_array(ARITH_OP_MUL, inPlace.data(), b.data(), inPlace.data(), n);

    size_t mismatches = 0;
    for (size_t i = 0; i < n; ++i)
        if (toBits(inPlace[i]) != toBits(arith_multiply(a[i], b[i])))
            ++mismatches;
    CHECK_EQ(mismatches, static_cast<size_t>(0));
}

TEST(the_scalar_variant_holds_b_constant)
{
    testutil::Rng rng(45);
    const size_t n = arith_parallel_threshold() + 1;
    const std::vector<float> a = randomFloats(rng, n);
    const float b = -2.5f;
    std::vector<float> out(n);

    for (ArithOp op : kOps) {
        arith_compute_array_scalar(op, a.data(), b, out.data(), n);
        size_t mismatches = 0;
        for (size_t i = 0; i < n; ++i)
            if (toBits(out[i]) != toBits(arith_compute(op, a[i], b)))
                ++mismatches;
        CHECK_EQ(mismatches, static_cast<size_t>(0));
    }
}

TEST(the_integer_batch_matches_the_primitives)
{
    testutil::Rng rng(46);
    const size_t n = arith_parallel_threshold() * 2;
    const std::vector<uint64_t> a = randomBits(rng, n);
    const std::vector<uint64_t> b = randomBits(rng, n);
    std::vector<uint64_t> out(n);

    const struct { ArithOp op; uint64_t (*fn)(uint64_t, uint64_t); } rows[] = {
        { ARITH_OP_ADD, &arith_add_bits },
        { ARITH_OP_SUB, &arith_sub_bits },
        { ARITH_OP_MUL, &arith_mul_bits },
        { ARITH_OP_DIV, &arith_div_bits },
    };
    for (const auto& row : rows) {
        arith_bits_array(row.op, a.data(), b.data(), out.data(), n);
        size_t mismatches = 0;
        for (size_t i = 0; i < n; ++i)
            if (out[i] != row.fn(a[i], b[i]))
                ++mismatches;
        CHECK_EQ(mismatches, static_cast<size_t>(0));
    }
}

TEST(batches_ignore_empty_and_null_arguments)
{
    const float a[4] = { 1.f, 2.f, 3.f, 4.f };
    const float b[4] = { 5.f, 6.f, 7.f, 8.f };
    float out[4] = { -1.f, -1.f, -1.f, -1.f };

    arith_compute_array(ARITH_OP_ADD, a, b, out, 0);          // nothing to do
    for (float v : out)
        CHECK_FLOAT_BITS(v, -1.f);

    arith_compute_array(ARITH_OP_ADD, nullptr, b, out, 4);    // must not crash or write
    arith_compute_array(ARITH_OP_ADD, a, nullptr, out, 4);
    arith_compute_array(ARITH_OP_ADD, a, b, nullptr, 4);
    arith_compute_array_scalar(ARITH_OP_ADD, nullptr, 1.f, out, 4);
    arith_compute_array_scalar(ARITH_OP_ADD, a, 1.f, nullptr, 4);
    arith_bits_array(ARITH_OP_ADD, nullptr, nullptr, nullptr, 4);
    for (float v : out)
        CHECK_FLOAT_BITS(v, -1.f);

    // ... and still work afterwards.
    arith_compute_array(ARITH_OP_ADD, a, b, out, 4);
    CHECK_FLOAT_BITS(out[0], 6.f);
    CHECK_FLOAT_BITS(out[3], 12.f);
}

TEST(the_parallel_runtime_reports_itself)
{
    // The projects build with /openmp; this fails if that gets turned off.
    CHECK_EQ(arith_parallel_available(), 1);
    CHECK(arith_parallel_threshold() > 0);
    CHECK(arith_parallel_threads() >= 1);

    arith_parallel_set_threads(2);
    CHECK_EQ(arith_parallel_threads(), 2);
    arith_parallel_set_threads(1);
    CHECK_EQ(arith_parallel_threads(), 1);
    arith_parallel_set_threads(0);             // restore the default
    CHECK(arith_parallel_threads() >= 1);
}
