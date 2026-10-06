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
#ifdef __ELF__
    endbr64
#endif
    movzbl (%rdi), %eax
    ret
.p2align 4
.globl CMETA_SYMBOL(cmeta_static_native_target)
#if defined(CMETA_STATIC_LIBRARY) && defined(__APPLE__)
.private_extern CMETA_SYMBOL(cmeta_static_native_target)
#elif defined(CMETA_STATIC_LIBRARY) && defined(__ELF__)
.hidden CMETA_SYMBOL(cmeta_static_native_target)
#endif
CMETA_SYMBOL(cmeta_static_native_target):
#ifdef __ELF__
    endbr64
#endif
    movq (%rdi), %rax
    ret
#ifdef __ELF__
.section .note.GNU-stack,"",@progbits
/* Both leaf entries support IBT and balanced shadow-stack call/ret. Marking
 * this object prevents linking it from clearing existing CET properties. */
#define CMETA_NOTE_ALIGN_LOG2 3
#define CMETA_NOTE_WORD_BYTES 4
#define CMETA_NT_GNU_PROPERTY_TYPE_0 5
#define CMETA_GNU_PROPERTY_X86_FEATURE_1_AND 0xc0000002
#define CMETA_GNU_PROPERTY_X86_FEATURE_1_IBT 1
#define CMETA_GNU_PROPERTY_X86_FEATURE_1_SHSTK 2
.section .note.gnu.property,"a",@note
.p2align CMETA_NOTE_ALIGN_LOG2
.long .Lname_end - .Lname
.long .Ldesc_end - .Ldesc
.long CMETA_NT_GNU_PROPERTY_TYPE_0
.Lname:
.asciz "GNU"
.Lname_end:
.p2align CMETA_NOTE_ALIGN_LOG2
.Ldesc:
.long CMETA_GNU_PROPERTY_X86_FEATURE_1_AND
.long CMETA_NOTE_WORD_BYTES
.long CMETA_GNU_PROPERTY_X86_FEATURE_1_IBT | CMETA_GNU_PROPERTY_X86_FEATURE_1_SHSTK
.p2align CMETA_NOTE_ALIGN_LOG2
.Ldesc_end:
#endif
