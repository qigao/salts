#ifdef __APPLE__
#define CMETA_SYMBOL(name) _##name
#else
#define CMETA_SYMBOL(name) name
#endif
.text
.p2align 4
.globl CMETA_SYMBOL(cmeta_static_branch_native)
#if defined(CMETA_STATIC_LIBRARY) && defined(__APPLE__)
.private_extern CMETA_SYMBOL(cmeta_static_branch_native)
#elif defined(CMETA_STATIC_LIBRARY) && defined(__ELF__)
.hidden CMETA_SYMBOL(cmeta_static_branch_native)
#endif
CMETA_SYMBOL(cmeta_static_branch_native):
    ldarb w0, [x0]
    ret
.p2align 4
.globl CMETA_SYMBOL(cmeta_static_native_target)
#if defined(CMETA_STATIC_LIBRARY) && defined(__APPLE__)
.private_extern CMETA_SYMBOL(cmeta_static_native_target)
#elif defined(CMETA_STATIC_LIBRARY) && defined(__ELF__)
.hidden CMETA_SYMBOL(cmeta_static_native_target)
#endif
CMETA_SYMBOL(cmeta_static_native_target):
    ldar x0, [x0]
    ret
#ifdef __ELF__
.section .note.GNU-stack,"",%progbits
#endif
