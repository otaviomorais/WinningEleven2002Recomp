/*
 * psx_fiber_android.c — ARM64 cooperative fiber backend for Android NDK / Bionic.
 * Bionic does not implement POSIX ucontext (makecontext/swapcontext).
 * This implementation provides ultra-fast direct register context switching for AArch64.
 */

#include "psx_fiber.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#if defined(__aarch64__)

typedef struct psx_fiber_impl {
    void*           sp;       /* saved SP for this fiber */
    void*           stack;    /* allocated buffer (NULL for main thread) */
    psx_fiber_entry entry;
    void*           arg;
} psx_fiber_impl;

static psx_fiber_impl* s_current = NULL;

extern void psx_fiber_switch_arm64(void** from_sp, void* to_sp);
extern void psx_fiber_trampoline_arm64(void);

__asm__(
    ".text\n"
    ".globl psx_fiber_switch_arm64\n"
    ".type psx_fiber_switch_arm64, %function\n"
    "psx_fiber_switch_arm64:\n"
    "    stp x19, x20, [sp, #-16]!\n"
    "    stp x21, x22, [sp, #-16]!\n"
    "    stp x23, x24, [sp, #-16]!\n"
    "    stp x25, x26, [sp, #-16]!\n"
    "    stp x27, x28, [sp, #-16]!\n"
    "    stp x29, x30, [sp, #-16]!\n"
    "    stp d8,  d9,  [sp, #-16]!\n"
    "    stp d10, d11, [sp, #-16]!\n"
    "    stp d12, d13, [sp, #-16]!\n"
    "    stp d14, d15, [sp, #-16]!\n"
    "    mov x2, sp\n"
    "    str x2, [x0]\n"
    "    mov sp, x1\n"
    "    ldp d14, d15, [sp], #16\n"
    "    ldp d12, d13, [sp], #16\n"
    "    ldp d10, d11, [sp], #16\n"
    "    ldp d8,  d9,  [sp], #16\n"
    "    ldp x29, x30, [sp], #16\n"
    "    ldp x27, x28, [sp], #16\n"
    "    ldp x25, x26, [sp], #16\n"
    "    ldp x23, x24, [sp], #16\n"
    "    ldp x21, x22, [sp], #16\n"
    "    ldp x19, x20, [sp], #16\n"
    "    ret\n"
    "\n"
    ".globl psx_fiber_trampoline_arm64\n"
    ".type psx_fiber_trampoline_arm64, %function\n"
    "psx_fiber_trampoline_arm64:\n"
    "    mov x0, x19\n"
    "    bl psx_fiber_run\n"
    "    bl abort\n"
);

void psx_fiber_run(psx_fiber_impl* f)
{
    f->entry(f->arg);
    abort();
}

psx_fiber_t psx_fiber_convert_thread(void)
{
    if (!s_current) {
        psx_fiber_impl* f = (psx_fiber_impl*)calloc(1, sizeof(*f));
        f->stack = NULL;
        s_current = f;
    }
    return (psx_fiber_t)s_current;
}

psx_fiber_t psx_fiber_current(void)
{
    return (psx_fiber_t)s_current;
}

psx_fiber_t psx_fiber_create(size_t stack_size, psx_fiber_entry entry, void* arg)
{
    if (stack_size < 32768) stack_size = 32768;
    psx_fiber_impl* f = (psx_fiber_impl*)calloc(1, sizeof(*f));
    if (!f) return NULL;
    f->stack = malloc(stack_size);
    if (!f->stack) { free(f); return NULL; }
    f->entry = entry;
    f->arg = arg;

    uintptr_t top = (uintptr_t)f->stack + stack_size;
    top &= ~((uintptr_t)15);
    top -= 160;
    uint64_t* regs = (uint64_t*)top;
    memset(regs, 0, 160);

    regs[8]  = 0;                                    /* x29 (fp) */
    regs[9]  = (uint64_t)psx_fiber_trampoline_arm64; /* x30 (lr) */
    regs[18] = (uint64_t)f;                          /* x19 */

    f->sp = (void*)top;
    return (psx_fiber_t)f;
}

void psx_fiber_switch(psx_fiber_t target)
{
    psx_fiber_impl* to = (psx_fiber_impl*)target;
    psx_fiber_impl* from = s_current;
    if (!to || to == from) return;
    s_current = to;
    psx_fiber_switch_arm64(&from->sp, to->sp);
}

void psx_fiber_destroy(psx_fiber_t fiber)
{
    psx_fiber_impl* f = (psx_fiber_impl*)fiber;
    if (!f) return;
    free(f->stack);
    free(f);
}

#else
#error "Android build requires ARM64 (aarch64) target architecture."
#endif
