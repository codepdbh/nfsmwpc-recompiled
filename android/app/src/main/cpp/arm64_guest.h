#pragma once
// Force-included only into locally generated C; Windows generation is untouched.
#include <time.h>
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
// (The NFSU2 port intercepts its resolution selector here; Speed.exe has none hooked yet.)
static inline uint64_t nfs_guest_rdtsc(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    // A stable virtual 1 GHz counter, not an ARM hardware cycle counter.
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}
#define __rdtsc() nfs_guest_rdtsc()
static inline void nfs_guest_cpuid(uint32_t leaf) {
    g_eax=g_ebx=g_ecx=g_edx=0;
    if (leaf==0) {
        g_eax=1; g_ebx=0x756e6547; g_edx=0x49656e69; g_ecx=0x6c65746e;
    } else if (leaf==1) {
        g_eax=0x00000663;
        // x87, TSC, CX8, CMOV, MMX, SSE, SSE2; no SSE3/AVX/3DNow.
        g_edx=((1u<<0)|(1u<<4)|(1u<<8)|(1u<<15)|(1u<<23)|(1u<<25)|(1u<<26)) & g_cpuid_edx1;
    } else if (leaf==0x80000000u) g_eax=0x80000001u;
}
#undef CPUID
#define CPUID(a,b,c,d) do { RECOMP_REGS_OUT(); nfs_guest_cpuid((a)); RECOMP_REGS_IN(); } while (0)
// One resolver per indirect call instead of manual -> lifted -> import probes
// (nfs_icall_resolve in guest_runtime.cpp, as in the NFSU2 port), and no
// g_icall_* trace: nothing on Android reads it, and its four global stores ran
// on each of the tens of thousands of D3D9 vtable calls of a race frame.
extern recomp_func_t nfs_icall_resolve(uint32_t va);
#undef RECOMP_ICALL
#define RECOMP_ICALL(target_va) do { \
    uint32_t _va = (uint32_t)(target_va); \
    recomp_func_t _fn = nfs_icall_resolve(_va); \
    if (_fn) { \
        uint32_t _caller = g_cur_func; \
        PUSH32(esp, RECOMP_RETADDR); \
        RECOMP_FLAGS_OUT(); RECOMP_REGS_OUT(); _fn(); RECOMP_REGS_IN(); \
        g_cur_func = _caller; \
    } else { RECOMP_MISS("ICALL", _va); eax = 0; } \
} while (0)
#undef RECOMP_ITAIL
#define RECOMP_ITAIL(target_va) do { \
    uint32_t _va = (uint32_t)(target_va); \
    recomp_func_t _fn = nfs_icall_resolve(_va); \
    if (_fn) { RECOMP_FLAGS_OUT(); RECOMP_REGS_OUT(); _fn(); RECOMP_REGS_IN(); } \
    else if (_va != RECOMP_RETADDR) { RECOMP_MISS("ITAIL", _va); } \
} while (0)
#ifdef NFS_GUEST_FIXED_BASE
// Guest memory is reserved at NFS_GUEST_FIXED_BASE (guest_memory.h): the base
// is an immediate instead of a global that any guest store might alias and
// that was therefore reloaded after each one.
#undef ADDR
#define ADDR(va) ((uintptr_t)(uint32_t)(va) + (uintptr_t)NFS_GUEST_FIXED_BASE)
#endif
#ifdef NFS_GUEST_NO_YIELD
// NFSU2 drops loop yields (it synchronises through Win32 waits). Most Wanted
// keeps them by default: a lifted spin-wait on another guest thread's flag would
// never hand the machine over. Opt in with -PnfsNoYield only after testing.
#undef RECOMP_BACKEDGE
#define RECOMP_BACKEDGE() ((void)0)
#endif
