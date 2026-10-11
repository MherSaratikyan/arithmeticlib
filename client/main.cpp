// arithmeticlib.client : console front-end for arithmeticlib.dll.
//
//   arithmeticlib.client 1.5 + 2.25          one-shot result, compared with native IEEE-754
//   arithmeticlib.client --trace 1 / 3       clock the state machine and print every cycle
//   arithmeticlib.client --bench 2000000 *   time a batch on one thread vs all of them
//   arithmeticlib.client                     interactive: "a op b", "trace a op b", "q"
#include <arithmeticlib.h>

#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _OPENMP
#  include <omp.h>
#else
#  include <chrono>
#endif

namespace {

uint32_t bitsOf(float f) { return std::bit_cast<uint32_t>(f); }

double seconds()
{
#ifdef _OPENMP
    return omp_get_wtime();
#else
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
#endif
}

bool parseOp(const std::string& s, ArithOp& op)
{
    if (s == "+")                  op = ARITH_OP_ADD;
    else if (s == "-")             op = ARITH_OP_SUB;
    else if (s == "*" || s == "x") op = ARITH_OP_MUL;
    else if (s == "/")             op = ARITH_OP_DIV;
    else return false;
    return true;
}

const char* opSymbol(ArithOp op)
{
    switch (op) {
    case ARITH_OP_ADD: return "+";
    case ARITH_OP_SUB: return "-";
    case ARITH_OP_MUL: return "*";
    case ARITH_OP_DIV: return "/";
    }
    return "?";
}

bool parseFloat(const std::string& s, float& out)
{
    char* end = nullptr;
    out = std::strtof(s.c_str(), &end);
    return end != s.c_str() && *end == '\0';
}

float native(ArithOp op, float a, float b)
{
    switch (op) {
    case ARITH_OP_ADD: return a + b;
    case ARITH_OP_SUB: return a - b;
    case ARITH_OP_MUL: return a * b;
    case ARITH_OP_DIV: return a / b;
    }
    return 0.0f;
}

void printResult(ArithOp op, float a, float b)
{
    const float r = arith_compute(op, a, b);
    const float n = native(op, a, b);

    std::printf("  %s: %.9g %s %.9g\n", arith_op_name(op), a, opSymbol(op), b);
    std::printf("    arithmeticlib : %-15.9g  0x%08X\n", r, bitsOf(r));
    std::printf("    native IEEE   : %-15.9g  0x%08X\n", n, bitsOf(n));
    if (bitsOf(r) != bitsOf(n)) {
        if (std::isnan(r) && std::isnan(n))
            std::printf("    (both NaN; the sign and payload of a NaN carry no meaning)\n");
        else if (!std::isfinite(r) || !std::isfinite(n))
            std::printf("    (results differ)\n");
        else
            std::printf("    (differs by %ld ulp: the algorithm truncates instead of rounding)\n",
                        static_cast<long>(bitsOf(n)) - static_cast<long>(bitsOf(r)));
    }
}

void printTrace(ArithOp op, float a, float b)
{
    ArithMachine* m = arith_machine_create(op, a, b);
    if (!m) {
        std::printf("  out of memory\n");
        return;
    }

    std::printf("  %s: %.9g %s %.9g\n\n", arith_op_name(op), a, opSymbol(op), b);
    std::printf("  cycle  state      alu     exp  %-18s %-18s %-18s %s\n",
        "mantissa/acc", "alu a", "alu b", "remainder");

    while (!arith_machine_is_done(m)) {
        ArithRegisters r{};
        arith_machine_registers(m, &r);
        const ArithState s = arith_machine_state(m);

        std::printf("  %5u  %-9s  %-6s  %3d  0x%016llX 0x%016llX 0x%016llX 0x%llX\n",
            arith_machine_cycles(m), arith_state_name(s), arith_alu_phase_name(r.alu_phase),
            r.exponent,
            s == ARITH_STATE_COMPUTE ? static_cast<unsigned long long>(r.alu_acc)
            : static_cast<unsigned long long>(r.mantissa),
            static_cast<unsigned long long>(r.alu_a),
            static_cast<unsigned long long>(r.alu_b),
            static_cast<unsigned long long>(r.alu_rem));
        arith_machine_step(m);

        if (s == ARITH_STATE_UNPACK) {
            // Unpack has just resolved the signs: show what the machine will
            // actually compute, since a + b can become a magnitude subtraction.
            ArithRegisters d{};
            arith_machine_registers(m, &d);
            std::printf("         signs: a %c, b %c  ->  computes %s, result %c\n",
                d.sign_a ? '-' : '+', d.sign_b ? '-' : '+',
                arith_mag_op_name(d.mag_op), d.negative ? '-' : '+');
        }
    }

    ArithRegisters r{};
    arith_machine_registers(m, &r);
    std::printf("  %5u  %-9s  sign=%d exp=%d mantissa=0x%06llX  ->  %.9g (0x%08X)\n\n",
        arith_machine_cycles(m), arith_state_name(arith_machine_state(m)),
        r.negative, r.exponent, static_cast<unsigned long long>(r.mantissa & 0x7FFFFF),
        arith_machine_result(m), r.result_bits);

    arith_machine_destroy(m);
}

// Drops a UTF-8 BOM and trailing CR/LF so piped or redirected Windows input parses.
std::string tidy(std::string line)
{
    if (line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0)
        line.erase(0, 3);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
        line.pop_back();
    return line;
}

// Parses "[trace] a op b"; returns false if the line is not an expression.
bool evaluate(const std::string& rawLine)
{
    const std::string line = tidy(rawLine);
    std::istringstream in(line);
    std::string first, opText, bText;
    in >> first;
    if (first.empty())
        return true;

    bool trace = false;
    if (first == "trace" || first == "--trace") {
        trace = true;
        in >> first;
    }
    in >> opText >> bText;

    float a = 0.0f, b = 0.0f;
    ArithOp op = ARITH_OP_ADD;
    if (!parseFloat(first, a) || !parseOp(opText, op) || !parseFloat(bText, b))
        return false;

    if (trace) printTrace(op, a, b);
    else       printResult(op, a, b);
    return true;
}

// Times one operation over a large batch, once on a single thread and once on
// all of them, and checks the two runs agree bit for bit.
void printBench(ArithOp op, size_t n)
{
    std::printf("  %s over %zu values, OpenMP %s\n", arith_op_name(op), n,
                arith_parallel_available() ? "on" : "off (built without /openmp)");

    std::vector<float> a(n), b(n), serial(n), parallel(n);
    uint64_t state = 0x2545F4914F6CDD1Dull;
    for (size_t i = 0; i < n; ++i) {
        // xorshift64, inline: operands in a range where no exponent overflows.
        state ^= state << 13; state ^= state >> 7; state ^= state << 17;
        a[i] = std::bit_cast<float>((static_cast<uint32_t>(state) & 0x807FFFFFu) | (120u << 23));
        state ^= state << 13; state ^= state >> 7; state ^= state << 17;
        b[i] = std::bit_cast<float>((static_cast<uint32_t>(state) & 0x807FFFFFu) | (124u << 23));
    }

    const int threads = arith_parallel_threads();

    arith_parallel_set_threads(1);
    const double t0 = seconds();
    arith_compute_array(op, a.data(), b.data(), serial.data(), n);
    const double oneThread = seconds() - t0;

    arith_parallel_set_threads(threads);
    const double t1 = seconds();
    arith_compute_array(op, a.data(), b.data(), parallel.data(), n);
    const double allThreads = seconds() - t1;

    size_t mismatches = 0;
    for (size_t i = 0; i < n; ++i)
        if (bitsOf(serial[i]) != bitsOf(parallel[i]))
            ++mismatches;

    std::printf("    1 thread    : %8.3f ms\n", oneThread * 1000.0);
    std::printf("    %2d threads  : %8.3f ms   (%.2fx)\n", threads, allThreads * 1000.0,
                allThreads > 0.0 ? oneThread / allThreads : 0.0);
    std::printf("    results     : %s\n",
                mismatches == 0 ? "identical, bit for bit"
                                : "DIFFER - the batch is not deterministic");
}

void usage()
{
    std::printf("usage: arithmeticlib.client [--trace] <a> <+|-|*|/> <b>\n"
                "       arithmeticlib.client --bench [count] [op]\n"
                "       arithmeticlib.client            (interactive)\n");
}

// Handles "--bench [count] [op]"; returns false if this is not a bench request.
bool tryBench(int argc, char** argv)
{
    if (argc < 2 || std::strcmp(argv[1], "--bench") != 0)
        return false;

    size_t n = 2000000;
    ArithOp op = ARITH_OP_MUL;
    if (argc > 2) {
        const long long requested = std::atoll(argv[2]);
        if (requested > 0)
            n = static_cast<size_t>(requested);
    }
    if (argc > 3)
        parseOp(argv[3], op);

    printBench(op, n);
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (tryBench(argc, argv))
        return 0;

    if (argc > 1) {
        std::string line;
        for (int i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
                usage();
                return 0;
            }
            if (std::strcmp(argv[i], "--") == 0)
                continue;                      // end-of-options separator
            line += argv[i];
            line += ' ';
        }
        if (!evaluate(line)) {
            usage();
            return 2;
        }
        return 0;
    }

    std::printf("arithmeticlib client - software float arithmetic as a state machine\n"
                "enter  a op b   (op: + - * /), prefix with 'trace' to watch the machine, 'q' to quit\n");
    std::string line;
    while (true) {
        std::printf("> ");
        if (!std::getline(std::cin, line))
            break;
        line = tidy(line);
        if (line == "q" || line == "quit" || line == "exit")
            break;

        // One expression is one operation: there is nothing here to split
        // across threads (`#pragma omp parallel` would just make every thread
        // print the same answer). Parallelism lives in the batch API, which
        // `--bench` exercises.
        if (!evaluate(line))
            std::printf("  could not parse; expected: [trace] a op b\n");
    }
    return 0;
}
