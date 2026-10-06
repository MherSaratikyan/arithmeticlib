// arithmeticlib.h : public C interface of arithmeticlib.dll
//
// Software floating-point add / subtract / multiply / divide for IEEE-754
// binary32 values, built only from bitwise operations and driven by an
// explicit state machine (see README.md for the state diagram).
//
// Operands may be positive or negative, including signed zero. Results are
// truncated rather than rounded, so they can be one ulp short of the
// correctly rounded value. Infinities, NaN and subnormal *inputs* are not
// interpreted; x / 0 does produce the IEEE result (+-inf, or NaN for 0 / 0).
#pragma once

#include <stdint.h>

#ifdef ARITHMETICLIB_EXPORTS
#  define ARITHMETICLIB_API __declspec(dllexport)
#else
#  define ARITHMETICLIB_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ArithOp {
    ARITH_OP_ADD = 0,
    ARITH_OP_SUB = 1,
    ARITH_OP_MUL = 2,
    ARITH_OP_DIV = 3
} ArithOp;

/* Pipeline stage the machine will execute on its next step. */
typedef enum ArithState {
    ARITH_STATE_UNPACK = 0,   /* split operands into exponent + mantissa        */
    ARITH_STATE_ALIGN,        /* equalise exponents / pre-scale, start the ALU  */
    ARITH_STATE_COMPUTE,      /* bit-serial ALU, one loop iteration per step    */
    ARITH_STATE_NORMALIZE,    /* fix mantissa overflow / leading zeros          */
    ARITH_STATE_PACK,         /* assemble the IEEE-754 bit pattern              */
    ARITH_STATE_DONE          /* result is available                            */
} ArithState;

/* The operation Unpack reduced the request to, once the signs were resolved:
   a + b with unlike signs is a magnitude subtraction, a - b with unlike signs
   is a magnitude addition, and so on. */
typedef enum ArithMagOp {
    ARITH_MAG_ADD = 0,        /* |a| + |b| */
    ARITH_MAG_SUB,            /* |a| - |b|, with |a| >= |b| */
    ARITH_MAG_MUL,            /* |a| * |b| */
    ARITH_MAG_DIV             /* |a| / |b| */
} ArithMagOp;

/* Phase of the bit-serial ALU while the machine is in ARITH_STATE_COMPUTE. */
typedef enum ArithAluPhase {
    ARITH_ALU_IDLE = 0,
    ARITH_ALU_ADD,            /* carry-ripple addition        (AddBits) */
    ARITH_ALU_NEGATE,         /* two's complement of b        (SubBits) */
    ARITH_ALU_MUL,            /* shift-and-add multiplication (MulBits) */
    ARITH_ALU_DIV,            /* restoring division           (DivBits) */
    ARITH_ALU_DONE
} ArithAluPhase;

/* Snapshot of the machine's registers, for tracing and tests. */
typedef struct ArithRegisters {
    int32_t  bits_a;          /* operand magnitudes, sign stripped; additive           */
    int32_t  bits_b;          /*   operations order them so |a| >= |b|                 */
    int32_t  sign_a;          /* operand signs; sign_b is flipped for Subtract,       */
    int32_t  sign_b;          /*   because a - b == a + (-b)                          */
    int32_t  exp_a;           /* biased exponents                                     */
    int32_t  exp_b;
    uint64_t man_a;           /* mantissas with the hidden bit set                    */
    uint64_t man_b;
    ArithMagOp mag_op;        /* operation carried out on the magnitudes              */
    uint64_t mantissa;        /* result mantissa (valid from Normalize on)            */
    int32_t  exponent;        /* result exponent, biased                              */
    int32_t  negative;        /* result sign                                          */
    uint32_t result_bits;     /* packed result (valid when Done)                      */
    ArithAluPhase alu_phase;  /* ALU registers, valid during Compute:                */
    uint64_t alu_a;           /*   sum / shifted multiplicand / dividend             */
    uint64_t alu_b;           /*   carries / remaining multiplier / divisor          */
    uint64_t alu_acc;         /*   partial product / quotient                        */
    uint64_t alu_rem;         /*   partial remainder (division)                      */
    int32_t  alu_bit;         /*   dividend bit being brought down (division)        */
    uint32_t alu_cycles;      /*   ALU iterations executed so far                    */
} ArithRegisters;

typedef struct ArithMachine ArithMachine;   /* opaque handle */

/* --- One-shot operations: run a machine to completion -------------------- */
ARITHMETICLIB_API float arith_add(float a, float b);
ARITHMETICLIB_API float arith_subtract(float a, float b);
ARITHMETICLIB_API float arith_multiply(float a, float b);
ARITHMETICLIB_API float arith_divide(float a, float b);
ARITHMETICLIB_API float arith_compute(ArithOp op, float a, float b);

/* --- Integer primitives (bit-serial ALU run to completion) ---------------- */
ARITHMETICLIB_API uint64_t arith_add_bits(uint64_t a, uint64_t b);
ARITHMETICLIB_API uint64_t arith_sub_bits(uint64_t a, uint64_t b);
ARITHMETICLIB_API uint64_t arith_mul_bits(uint64_t a, uint64_t b);
ARITHMETICLIB_API uint64_t arith_div_bits(uint64_t a, uint64_t b);

/* --- Stepwise state machine ---------------------------------------------- */
ARITHMETICLIB_API ArithMachine* arith_machine_create(ArithOp op, float a, float b);
ARITHMETICLIB_API void          arith_machine_destroy(ArithMachine* m);
ARITHMETICLIB_API void          arith_machine_reset(ArithMachine* m, ArithOp op, float a, float b);
ARITHMETICLIB_API int           arith_machine_step(ArithMachine* m);          /* 1 once Done     */
ARITHMETICLIB_API float         arith_machine_run(ArithMachine* m);           /* step until Done */
ARITHMETICLIB_API ArithState    arith_machine_state(const ArithMachine* m);
ARITHMETICLIB_API int           arith_machine_is_done(const ArithMachine* m);
ARITHMETICLIB_API float         arith_machine_result(const ArithMachine* m);  /* 0.0f until Done */
ARITHMETICLIB_API uint32_t      arith_machine_cycles(const ArithMachine* m);  /* steps executed  */
ARITHMETICLIB_API void          arith_machine_registers(const ArithMachine* m, ArithRegisters* out);

/* --- Names for diagnostics ----------------------------------------------- */
ARITHMETICLIB_API const char* arith_op_name(ArithOp op);
ARITHMETICLIB_API const char* arith_mag_op_name(ArithMagOp op);
ARITHMETICLIB_API const char* arith_state_name(ArithState s);
ARITHMETICLIB_API const char* arith_alu_phase_name(ArithAluPhase p);

#ifdef __cplusplus
}
#endif
