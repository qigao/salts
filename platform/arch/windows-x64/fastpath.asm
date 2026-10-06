; Leaf acquire loads under the Windows x64 memory-ordering and return ABI.
.code
PUBLIC salts_static_branch_native
salts_static_branch_native PROC
    movzx eax, BYTE PTR [rcx]
    ret
salts_static_branch_native ENDP

PUBLIC salts_static_native_target
salts_static_native_target PROC
    mov rax, QWORD PTR [rcx]
    ret
salts_static_native_target ENDP
END
