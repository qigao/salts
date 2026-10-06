#ifdef __APPLE__
#define SALTS_SYMBOL(name) _##name
#else
#define SALTS_SYMBOL(name) name
#endif
.text
.p2align 4
.globl SALTS_SYMBOL(salts_static_branch_native)
#if defined(SALTS_FASTPATH_STATIC_LIBRARY) && defined(__APPLE__)
.private_extern SALTS_SYMBOL(salts_static_branch_native)
#elif defined(SALTS_FASTPATH_STATIC_LIBRARY) && defined(__ELF__)
.hidden SALTS_SYMBOL(salts_static_branch_native)
#endif
SALTS_SYMBOL(salts_static_branch_native):
    ldarb w0, [x0]
    ret
.p2align 4
.globl SALTS_SYMBOL(salts_static_native_target)
#if defined(SALTS_FASTPATH_STATIC_LIBRARY) && defined(__APPLE__)
.private_extern SALTS_SYMBOL(salts_static_native_target)
#elif defined(SALTS_FASTPATH_STATIC_LIBRARY) && defined(__ELF__)
.hidden SALTS_SYMBOL(salts_static_native_target)
#endif
SALTS_SYMBOL(salts_static_native_target):
    ldar x0, [x0]
    ret
#ifdef __ELF__
.section .note.GNU-stack,"",%progbits
#endif
