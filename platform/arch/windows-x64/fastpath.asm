; Leaf acquire loads under the Windows x64 memory-ordering and return ABI.
.code
PUBLIC cmeta_fast_key_read_native
cmeta_fast_key_read_native PROC
    movzx eax, BYTE PTR [rcx]
    ret
cmeta_fast_key_read_native ENDP

PUBLIC cmeta_fast_target_load_native
cmeta_fast_target_load_native PROC
    mov rax, QWORD PTR [rcx]
    ret
cmeta_fast_target_load_native ENDP
END
