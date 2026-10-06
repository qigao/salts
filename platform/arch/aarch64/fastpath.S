#ifdef __APPLE__
#define SALTS_SYMBOL(name) _##name
#else
#define SALTS_SYMBOL(name) name
#endif
.text
.p2align 4
.globl SALTS_SYMBOL(salts_fast_key_read_native)
#if defined(SALTS_PLATFORM_STATIC_LIBRARY) && defined(__APPLE__)
.private_extern SALTS_SYMBOL(salts_fast_key_read_native)
#elif defined(SALTS_PLATFORM_STATIC_LIBRARY) && defined(__ELF__)
.hidden SALTS_SYMBOL(salts_fast_key_read_native)
#endif
SALTS_SYMBOL(salts_fast_key_read_native):
    ldarb w0, [x0]
    ret
.p2align 4
.globl SALTS_SYMBOL(salts_fast_target_load_native)
#if defined(SALTS_PLATFORM_STATIC_LIBRARY) && defined(__APPLE__)
.private_extern SALTS_SYMBOL(salts_fast_target_load_native)
#elif defined(SALTS_PLATFORM_STATIC_LIBRARY) && defined(__ELF__)
.hidden SALTS_SYMBOL(salts_fast_target_load_native)
#endif
SALTS_SYMBOL(salts_fast_target_load_native):
    ldar x0, [x0]
    ret
#ifdef __ELF__
.section .note.GNU-stack,"",%progbits
#endif
