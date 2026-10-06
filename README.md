# arithmeticlib

Software floating-point **add / subtract / multiply / divide** for IEEE-754
`float`, built purely from bitwise operations and driven by an explicit
**state machine**. Operands may be **positive or negative**, including signed
zero. A C++ port of the C# `ArithmeticModule`, packaged as a Windows DLL with
a C API, a test suite and a console client.

```
arithmeticlib/
├── arithmeticlib.slnx            solution (DLL + tests + client)
├── arithmeticlib.vcxproj         the DLL
│   ├── arithmeticlib.h           public C API (exported functions, enums, register snapshot)
│   ├── arithmeticlib.cpp         C API implementation
│   ├── arithmetic_machine.h/.cpp float operations as a 6-state pipeline
│   └── bit_serial_alu.h/.cpp     AddBits/SubBits/MulBits/DivBits as a clocked sub-machine
├── tests/                        console test runner (no external dependencies)
│   ├── reference.h               transliterated C# module + sign rules = the oracle
│   └── test_*.cpp                60 tests
└── client/                       console client: one-shot results and cycle-by-cycle traces
```

## The state machine

Every operation walks the same pipeline. `step()` executes one state; only
**Compute** (and **Normalize** for a magnitude subtraction) take a
data-dependent number of steps.

```
  Unpack ──> Align ──> Compute ──> Normalize ──> Pack ──> Done
     │                   ^  │         ^  │        ^
     │                   └──┘         └──┘        │
     │             one ALU cycle    one shift     │
     └─────────────────────────────────────────── ┘
        trivial result: a zero operand, cancellation, division by zero
```

| State       | What happens                                                                     |
|-------------|----------------------------------------------------------------------------------|
| `Unpack`    | Resolve the signs (see below), strip them, and split both magnitudes into biased exponent and 24-bit mantissa (hidden bit set). Jump straight to `Pack` when the result already follows from the operands. |
| `Align`     | Add/Sub: shift the smaller mantissa right by the exponent difference. Div: pre-shift the dividend by 23 bits. Compute the result exponent. Load the ALU. |
| `Compute`   | Clock the bit-serial ALU once per step until it settles.                         |
| `Normalize` | Add: carry out of bit 24 → shift right, exponent+1. Sub: shift left one leading zero per step. Mul: drop 23 or 24 bits. Div: shift left once if the quotient is below 1. |
| `Pack`      | `sign │ exponent << 23 │ mantissa & 0x7FFFFF`.                                   |
| `Done`      | Result available; further steps are no-ops.                                      |

### Signs

`Unpack` is the only state that knows about signs. It reduces the requested
operation to an operation on **magnitudes** plus a result sign, and everything
downstream works on positive values exactly like the unsigned original:

| Request | Signs    | Magnitude operation | Result sign      |
|---------|----------|---------------------|------------------|
| `a + b` | alike    | `\|a\| + \|b\|`     | sign of `a`      |
| `a + b` | unlike   | `\|a\| - \|b\|`     | sign of the larger magnitude |
| `a - b` | —        | same as `a + (-b)`  | —                |
| `a * b` | —        | `\|a\| * \|b\|`     | `signA XOR signB`|
| `a / b` | —        | `\|a\| / \|b\|`     | `signA XOR signB`|

For the additive cases the operands are swapped if needed so that
`|a| >= |b|`: `Align` then only ever shifts `b` right, and the result takes
the sign of `a`. `ArithRegisters.mag_op` reports what was chosen, so a trace
shows `1 + -2` computing `|a| - |b|`.

Results that need no ALU work are settled in `Unpack` and reach `Done` in two
cycles: a zero operand (the other operand is copied through bit for bit),
equal magnitudes of unlike sign (`x + -x` → `+0`), and division by zero
(`±inf`, or NaN for `0 / 0`).

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
ArithMachine* m = arith_machine_create(ARITH_OP_DIV, -1.0f, 3.0f);
while (!arith_machine_step(m)) {
    ArithRegisters r;
    arith_machine_registers(m, &r);           // signs, mag_op, exponent, mantissas, ALU, ...
    printf("%s %s\n", arith_state_name(arith_machine_state(m)),
                      arith_mag_op_name(r.mag_op));
}
float q = arith_machine_result(m);            // 0xBEAAAAAA
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

## Using the DLL from your own project

A DLL needs three things to be usable: the **header** (declarations), the
**import library** `arithmeticlib.lib` (what the linker binds against) and the
**`arithmeticlib.dll`** itself (loaded at run time, so it must sit next to
your `.exe`). Pick whichever route matches your project.

**From a C++ project inside this solution** — the way `tests/` and `client/`
do it, and the one that needs no manual copying:

1. Add your project to `arithmeticlib.slnx`.
2. Right-click your project → **Add** → **Reference** → tick `arithmeticlib`.
   Visual Studio then builds the DLL first, links `arithmeticlib.lib`
   automatically, and copies the DLL next to your executable.
3. Add the DLL's folder to **C/C++ → General → Additional Include
   Directories** (`$(SolutionDir)`), then `#include <arithmeticlib.h>`.
4. Build. Keep your project's **Platform** (`x64`/`Win32`) and
   **Configuration** (`Debug`/`Release`) the same as the DLL's — a 64-bit
   executable cannot load a 32-bit DLL.

**From a C++ project outside this solution:**

1. Build the DLL: `msbuild arithmeticlib.vcxproj /p:Configuration=Release
   /p:Platform=x64`.
2. Copy `arithmeticlib.h` and, from `bin\x64\Release\`, `arithmeticlib.lib`
   and `arithmeticlib.dll` into your project.
3. In your project: **C/C++ → Additional Include Directories** → the folder
   holding the header; **Linker → Input → Additional Dependencies** → add
   `arithmeticlib.lib`; **Linker → General → Additional Library Directories**
   → the folder holding the `.lib`.
4. Make sure `arithmeticlib.dll` ends up in the same folder as your `.exe`
   (a Post-Build Event such as
   `xcopy /y "$(SolutionDir)bin\$(Platform)\$(Configuration)\arithmeticlib.dll" "$(OutDir)"`
   does it). Without this the program builds but fails to start with
   *"arithmeticlib.dll was not found"*.

**From C#** — no header or `.lib` needed, just the DLL beside your executable:

```csharp
using System.Runtime.InteropServices;

internal static class Arith
{
    const string Dll = "arithmeticlib.dll";

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern float arith_add(float a, float b);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern float arith_divide(float a, float b);
}

Console.WriteLine(Arith.arith_divide(-1f, 3f));   // -0.33333331
```

Set the C# project's **Platform target** to match the DLL (`x64` for an x64
build; `AnyCPU` with *Prefer 32-bit* unticked also works on a 64-bit machine),
and add the DLL to the project with **Copy to Output Directory = Copy if
newer**.

Quick check that everything is wired up: run
`bin\x64\Release\arithmeticlib.client.exe 1 / 3`. If that works, the DLL is
fine and any failure is in your own project's settings.

## Tests

```bat
bin\x64\Release\arithmeticlib.tests.exe            # all 60 tests
bin\x64\Release\arithmeticlib.tests.exe sign       # only tests whose name contains "sign"
```

The suite checks four things:

* **Primitives** – `arith_*_bits` against native 64-bit integer arithmetic and
  against the reference, including wrap-around and division by zero.
* **Operations** – exact cases (results representable in `float`, so
  truncation and IEEE rounding must agree bit for bit), documented truncation
  cases (`1/3 → 0x3EAAAAAA`, native gives `0x3EAAAAAB`), and 5000 random
  positive normals per operation compared bit for bit with `reference.h`,
  the transliterated C# module.
* **Signs** – every sign combination of exact cases against native IEEE,
  `a - b == a + (-b)`, cancellation, signed zero, `±inf` and NaN from division
  by zero, and 5000 random *signed* normals per operation against the
  reference's sign layer.
* **Machine** – state order, exact cycle counts (24 for multiply, 64 for
  divide, 2 for a trivial result), ALU phases (`Negate` then `Add` for a
  magnitude subtraction), the sign and `mag_op` registers, reset, stepping
  past `Done`, null handles.

Exit code is 0 when everything passes.

## Client

```
> bin\x64\Release\arithmeticlib.client.exe 1 / 3
  Divide: 1 / 3
    arithmeticlib : 0.333333313      0x3EAAAAAA
    native IEEE   : 0.333333343      0x3EAAAAAB
    (differs by 1 ulp: the algorithm truncates instead of rounding)

> bin\x64\Release\arithmeticlib.client.exe --trace -1.5 + 2.25
  Add: -1.5 + 2.25

  cycle  state      alu     exp  mantissa/acc       alu a              alu b              remainder
      0  Unpack     Idle      0  0x0000000000000000 0x0000000000000000 0x0000000000000000 0x0
         signs: a +, b -  ->  computes |a|-|b|, result +
      1  Align      Idle      0  0x0000000000000000 0x0000000000000000 0x0000000000000000 0x0
      2  Compute    Negate  128  0x0000000000000000 0xFFFFFFFFFF9FFFFF 0x0000000000000001 0x0
      ...
     64  Compute    Add     128  0x0000000000000000 0x8000000000300000 0x8000000000000000 0x0
     65  Normalize  Done    128  0x0000000000300000 0x0000000000300000 0x0000000000000000 0x0
     66  Normalize  Done    127  0x0000000000600000 0x0000000000300000 0x0000000000000000 0x0
     67  Normalize  Done    126  0x0000000000C00000 0x0000000000300000 0x0000000000000000 0x0
     68  Pack       Done    126  0x0000000000C00000 0x0000000000300000 0x0000000000000000 0x0
     69  Done       sign=0 exp=126 mantissa=0x400000  ->  0.75 (0x3F400000)
```

Run it without arguments for an interactive prompt (`a op b`,
`trace a op b`, `q`). Negative operands work in both modes; put a `--` first
if a leading minus confuses your shell.

## Semantics and limitations

The port keeps the reference algorithm as is; it is a teaching model, not an
IEEE-754 implementation:

* **Signed normal numbers and signed zero** are handled, and so is division by
  zero (`±inf`, NaN for `0 / 0`). Infinity, NaN and subnormal *inputs* are not
  interpreted — their exponent and hidden bit are decoded like any other
  operand, which gives nonsense. (Adding zero is the exception: it copies the
  other operand through unchanged, whatever it is.)
* **Truncation, no rounding.** A magnitude addition, multiply or divide drops
  the low bits of its own result, so it lands up to one ulp (two for a divide
  whose quotient is below 1) short of the correctly rounded value, towards
  zero. A magnitude subtraction instead truncates the operand being aligned,
  which makes the difference slightly *too large*; there is no guard bit, so
  cancellation can magnify that (`1 - (1 - 2^-24)` gives `2^-23`, twice the
  exact answer).
* **No overflow/underflow detection.** The result exponent is packed modulo
  256. It is masked, so an overflow wraps rather than spilling into the sign
  bit: the sign of a result is always correct.
* **One deliberate difference from C#:** a right shift by 64 or more is
  undefined in C++ (C# masks the count to 6 bits, so `x >> 70` silently
  becomes `x >> 6`). The DLL flushes such an operand to zero, which is what
  the alignment step means: `2^70 + 1 == 2^70`. The random tests keep the
  exponent difference below 64 so both implementations can be compared.
