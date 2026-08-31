// ============================================================
//  abi.h — calling-convention abstraction for generated code
//
//  The JIT-generated code must call (a) other generated Omnikarai
//  functions and (b) the C runtime helpers compiled into the omnicc
//  host process. Both must agree on ONE calling convention, which
//  is selected at host-compile time:
//
//    OMNI_ABI_WIN64 (default on Windows):
//      args   RCX, RDX, R8, R9  (float args XMM0–3)
//      caller reserves 32-byte shadow space
//      callee-saved: RBX, RBP, RDI, RSI, R12–R15
//      → up to 5 pinned variable registers (RBX,R12,R13,RSI,RDI)
//
//    OMNI_ABI_SYSV (default on Linux/macOS, System V AMD64):
//      args   RDI, RSI, RDX, RCX, R8, R9  (float args XMM0–7)
//      no shadow space; RSP 16-byte aligned at every CALL
//      callee-saved: RBX, RBP, R12–R15 (RSI/RDI are VOLATILE)
//      → up to 3 pinned variable registers (RBX,R12,R13)
//
//  Return value: RAX (int/pointer) or XMM0 (double) in both ABIs.
//
//  NOTE for SysV: because RSI/RDI are caller-saved there, they can
//  never hold pinned variables across a call. OMNI_MAX_PIN enforces
//  the reduced pin set; the RSI/RDI slot code paths become dead.
// ============================================================
#ifndef OMNI_ABI_H
#define OMNI_ABI_H

#include <stdint.h>

#if defined(_WIN32)
#  define OMNI_ABI_WIN64 1
#else
#  define OMNI_ABI_SYSV 1
#endif

#if defined(OMNI_ABI_WIN64)
#  define OMNI_MAX_PIN 5
#else
#  define OMNI_MAX_PIN 3
#endif

#endif /* OMNI_ABI_H */
