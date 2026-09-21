// test_framework.h : tiny self-contained test harness (no external dependencies).
//
//   TEST(name) { CHECK(...); CHECK_EQ(a, b); CHECK_FLOAT_BITS(x, y); }
//
// Tests self-register; testfw::runAll() runs them (optionally filtered by a
// substring given as the first command-line argument) and returns the exit code.
#pragma once

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace testfw {

struct Test {
    const char* name;
    void (*fn)();
};

inline std::vector<Test>& tests()
{
    static std::vector<Test> list;
    return list;
}

inline int& failedChecks()
{
    static int count = 0;
    return count;
}

struct Register {
    Register(const char* name, void (*fn)()) { tests().push_back({ name, fn }); }
};

// Value formatting for failure messages.
inline std::string show(bool v)               { return v ? "true" : "false"; }
inline std::string show(int v)                { return std::to_string(v); }
inline std::string show(long v)               { return std::to_string(v); }
inline std::string show(long long v)          { return std::to_string(v); }
inline std::string show(unsigned v)           { return std::to_string(v); }
inline std::string show(unsigned long v)      { return std::to_string(v); }
inline std::string show(const char* v)        { return v ? std::string("\"") + v + "\"" : "nullptr"; }
inline std::string show(const std::string& v) { return "\"" + v + "\""; }

inline std::string show(unsigned long long v)
{
    char buf[48];
    std::snprintf(buf, sizeof buf, "%llu (0x%llX)", v, v);
    return buf;
}

inline std::string show(float v)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.9g (0x%08X)", static_cast<double>(v), std::bit_cast<uint32_t>(v));
    return buf;
}

inline void fail(const char* file, int line, const std::string& message)
{
    ++failedChecks();
    std::printf("    FAILED %s(%d): %s\n", file, line, message.c_str());
}

template <class A, class B>
void checkEq(const char* file, int line, const char* exprA, const char* exprB, const A& a, const B& b)
{
    if (!(a == b))
        fail(file, line, std::string(exprA) + " == " + exprB
                         + "\n        left:  " + show(a)
                         + "\n        right: " + show(b));
}

inline bool sameBits(float a, float b)
{
    return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b);
}

inline void checkFloatBits(const char* file, int line, const char* exprA, const char* exprB, float a, float b)
{
    if (!sameBits(a, b))
        fail(file, line, std::string(exprA) + " has the same bits as " + exprB
                         + "\n        left:  " + show(a)
                         + "\n        right: " + show(b));
}

inline int runAll(int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0, failedTests = 0;

    for (const Test& t : tests()) {
        if (filter && !std::strstr(t.name, filter))
            continue;
        ++ran;
        const int before = failedChecks();
        t.fn();
        const bool ok = failedChecks() == before;
        if (!ok)
            ++failedTests;
        std::printf("[%s] %s\n", ok ? " OK " : "FAIL", t.name);
    }

    std::printf("\n%d test(s) run: %d passed, %d failed (%d failed check(s))\n",
                ran, ran - failedTests, failedTests, failedChecks());
    return failedTests == 0 ? 0 : 1;
}

} // namespace testfw

#define TEST(name)                                                       \
    static void name();                                                  \
    static const testfw::Register name##_registration(#name, &name);     \
    static void name()

#define CHECK(expr)                                                      \
    do {                                                                 \
        if (!(expr))                                                     \
            testfw::fail(__FILE__, __LINE__, "CHECK(" #expr ")");        \
    } while (0)

#define CHECK_EQ(a, b) testfw::checkEq(__FILE__, __LINE__, #a, #b, (a), (b))

#define CHECK_FLOAT_BITS(a, b) testfw::checkFloatBits(__FILE__, __LINE__, #a, #b, (a), (b))
