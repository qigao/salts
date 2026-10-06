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
#ifdef __ELF__
    endbr64
#endif
    movzbl (%rdi), %eax
    ret
.p2align 4
.globl SALTS_SYMBOL(salts_fast_target_load_native)
#if defined(SALTS_PLATFORM_STATIC_LIBRARY) && defined(__APPLE__)
.private_extern SALTS_SYMBOL(salts_fast_target_load_native)
#elif defined(SALTS_PLATFORM_STATIC_LIBRARY) && defined(__ELF__)
.hidden SALTS_SYMBOL(salts_fast_target_load_native)
#endif
SALTS_SYMBOL(salts_fast_target_load_native):
#ifdef __ELF__
    endbr64
#endif
    movq (%rdi), %rax
    ret
#ifdef __ELF__
.section .note.GNU-stack,"",@progbits
/* Both leaf entries support IBT and balanced shadow-stack call/ret. Marking
 * this object prevents linking it from clearing existing CET properties. */
#define SALTS_NOTE_ALIGN_LOG2 3
#define SALTS_NOTE_WORD_BYTES 4
#define SALTS_NT_GNU_PROPERTY_TYPE_0 5
#define SALTS_GNU_PROPERTY_X86_FEATURE_1_AND 0xc0000002
#define SALTS_GNU_PROPERTY_X86_FEATURE_1_IBT 1
#define SALTS_GNU_PROPERTY_X86_FEATURE_1_SHSTK 2
.section .note.gnu.property,"a",@note
.p2align SALTS_NOTE_ALIGN_LOG2
.long .Lname_end - .Lname
.long .Ldesc_end - .Ldesc
.long SALTS_NT_GNU_PROPERTY_TYPE_0
.Lname:
.asciz "GNU"
.Lname_end:
.p2align SALTS_NOTE_ALIGN_LOG2
.Ldesc:
.long SALTS_GNU_PROPERTY_X86_FEATURE_1_AND
.long SALTS_NOTE_WORD_BYTES
.long SALTS_GNU_PROPERTY_X86_FEATURE_1_IBT | SALTS_GNU_PROPERTY_X86_FEATURE_1_SHSTK
.p2align SALTS_NOTE_ALIGN_LOG2
.Ldesc_end:
#endif
