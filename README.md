# arithmeticlib

Software floating-point **add / subtract / multiply / divide** for IEEE-754
`float`, built purely from bitwise operations and driven by an explicit
**state machine**. A C++ port of the C# `ArithmeticModule`, packaged as a
Windows DLL with a C API, a test suite and a console client.

```
arithmeticlib/
├── arithmeticlib.slnx            solution (DLL + tests + client)
├── arithmeticlib.vcxproj         the DLL
│   ├── arithmeticlib.h           public C API (exported functions, enums, register snapshot)
│   ├── arithmeticlib.cpp         C API implementation
│   ├── arithmetic_machine.h/.cpp float operations as a 6-state pipeline
│   └── bit_serial_alu.h/.cpp     AddBits/SubBits/MulBits/DivBits as a clocked sub-machine
├── tests/                        console test runner (no external dependencies)
│   ├── reference.h               line-for-line transliteration of the C# module = the oracle
│   └── test_*.cpp                44 tests
└── client/                       console client: one-shot results and cycle-by-cycle traces
```

## The state machine

Every operation walks the same pipeline. `step()` executes one state; only
**Compute** (and **Normalize** for Subtract) take a data-dependent number of
steps.

```
  Unpack ──> Align ──> Compute ──> Normalize ──> Pack ──> Done
                         ^  │         ^  │
                         └──┘         └──┘
                   one ALU cycle    one leading-zero shift (Subtract)
```

| State       | What happens                                                                     |
|-------------|----------------------------------------------------------------------------------|
| `Unpack`    | Split both operands into biased exponent and 24-bit mantissa (hidden bit set). Subtract orders the operands so the larger bit pattern is the minuend and remembers the sign. |
| `Align`     | Add/Sub: shift the smaller mantissa right by the exponent difference. Div: pre-shift the dividend by 23 bits. Compute the result exponent. Load the ALU. |
| `Compute`   | Clock the bit-serial ALU once per step until it settles.                         |
| `Normalize` | Add: carry out of bit 24 → shift right, exponent+1. Sub: shift left one leading zero per step (or produce +0). Mul: drop 23 or 24 bits. Div: shift left once if the quotient is below 1. |
| `Pack`      | `sign | exponent << 23 | mantissa & 0x7FFFFF`.                                   |
| `Done`      | Result available; further steps are no-ops.                                      |

The ALU inside `Compute` is itself a small state machine (`BitSerialAlu`);
each of its steps is exactly one iteration of the corresponding C# loop:

| ALU phase | Loop in the C# source     | Cycles                            |
|-----------|---------------------------|-----------------------------------|
| `Add`     | `AddBits` carry ripple    | length of the carry chain (≤ 64)  |
| `Negate`  | `AddBits(~b, 1)` (SubBits)| carry chain, then continues in `Add` |
| `Mul`     | `MulBits` shift-and-add   | 24 (one per multiplier bit)       |
| `Div`     | `DivBits` restoring division | 64 (one per dividend bit)      |

So `3 * 4` takes 1 + 1 + 24 + 1 + 1 = 28 cycles and any division takes 68.

## API

```c
#include "arithmeticlib.h"

// one-shot
float arith_add(float a, float b);
float arith_subtract(float a, float b);
float arith_multiply(float a, float b);
float arith_divide(float a, float b);
float arith_compute(ArithOp op, float a, float b);

// integer primitives (run to completion)
uint64_t arith_add_bits(uint64_t a, uint64_t b);   // also sub/mul/div

// stepwise
ArithMachine* m = arith_machine_create(ARITH_OP_DIV, 1.0f, 3.0f);
while (!arith_machine_step(m)) {
    ArithRegisters r;
    arith_machine_registers(m, &r);           // exponent, mantissas, ALU registers, ...
    printf("%s\n", arith_state_name(arith_machine_state(m)));
}
float q = arith_machine_result(m);            // 0x3EAAAAAA
arith_machine_destroy(m);
```

All functions are `extern "C"` and use the default (`__cdecl`) calling
convention, so the DLL is consumable from C, C++ and via P/Invoke:

```csharp
[DllImport("arithmeticlib.dll", CallingConvention = CallingConvention.Cdecl)]
static extern float arith_divide(float a, float b);
```

## Building

Open `arithmeticlib.slnx` in Visual Studio 2026 (v145 toolset) and build, or
from a developer command prompt:

```bat
msbuild arithmeticlib.slnx /p:Configuration=Release /p:Platform=x64
```

All three projects output to `bin\<Platform>\<Configuration>\`, so the test
and client executables find the DLL next to them. `x86` builds the `Win32`
configurations.

## Tests

```bat
bin\x64\Release\arithmeticlib.tests.exe            # all 44 tests
bin\x64\Release\arithmeticlib.tests.exe machine    # only tests whose name contains "machine"
```

The suite checks three things:

* **Primitives** – `arith_*_bits` against native 64-bit integer arithmetic and
  against the reference, including wrap-around and division by zero.
* **Operations** – exact cases (results representable in `float`, so
  truncation and IEEE rounding must agree bit for bit), documented truncation
  cases (`1/3 → 0x3EAAAAAA`, native gives `0x3EAAAAAB`), and 5000 random
  positive normals per operation compared bit for bit with `reference.h`,
  the transliterated C# module.
* **Machine** – state order, exact cycle counts (24 for multiply, 64 for
  divide), ALU phases (`Negate` then `Add` for subtract), register snapshots,
  reset, stepping past `Done`, null handles.

Exit code is 0 when everything passes.

## Client

```
> bin\x64\Release\arithmeticlib.client.exe 1 / 3
  Divide: 1 / 3
    arithmeticlib : 0.333333313      0x3EAAAAAA
    native IEEE   : 0.333333343      0x3EAAAAAB
    (differs by 1 ulp: the algorithm truncates instead of rounding)

> bin\x64\Release\arithmeticlib.client.exe --trace 3 * 4
  Multiply: 3 * 4

  cycle  state      alu     exp  mantissa/acc       alu a              alu b              remainder
      0  Unpack     Idle      0  0x0000000000000000 0x0000000000000000 0x0000000000000000 0x0
      1  Align      Idle      0  0x0000000000000000 0x0000000000000000 0x0000000000000000 0x0
      2  Compute    Mul     130  0x0000000000000000 0x0000000000C00000 0x0000000000800000 0x0
      ...
     25  Compute    Mul     130  0x0000000000000000 0x0000600000000000 0x0000000000000001 0x0
     26  Normalize  Done    130  0x0000600000000000 0x0000C00000000000 0x0000000000000000 0x0
     27  Pack       Done    130  0x0000000000C00000 0x0000C00000000000 0x0000000000000000 0x0
     28  Done       sign=0 exp=130 mantissa=0x400000  ->  12 (0x41400000)
```

Run it without arguments for an interactive prompt (`a op b`,
`trace a op b`, `q`).

## Semantics and limitations

The port keeps the reference algorithm as is; it is a teaching model, not an
IEEE-754 implementation:

* **Positive normal numbers only.** Signs are ignored except for the sign of a
  subtraction result; zero, subnormals, infinities and NaN are not handled.
* **Truncation, no rounding.** Bits shifted out during alignment or after the
  multiply/divide are dropped, so results can sit one ulp (two for a divide
  whose quotient is below 1) under the correctly rounded value.
* **No overflow/underflow detection.** The exponent is packed modulo 2^32.
* **One deliberate difference from C#:** a right shift by 64 or more is
  undefined in C++ (C# masks the count to 6 bits, so `x >> 70` silently
  becomes `x >> 6`). The DLL flushes such an operand to zero, which is what
  the alignment step means: `2^70 + 1 == 2^70`. The random tests keep the
  exponent difference below 64 so both implementations can be compared.
