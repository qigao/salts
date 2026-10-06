; Leaf acquire loads under the Windows x64 memory-ordering and return ABI.
.code
PUBLIC salts_fast_key_read_native
salts_fast_key_read_native PROC
    movzx eax, BYTE PTR [rcx]
    ret
salts_fast_key_read_native ENDP

PUBLIC salts_fast_target_load_native
salts_fast_target_load_native PROC
    mov rax, QWORD PTR [rcx]
    ret
salts_fast_target_load_native ENDP
END
