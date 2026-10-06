; Leaf acquire loads under the Windows x64 memory-ordering and return ABI.
.code
PUBLIC cmeta_static_branch_native
cmeta_static_branch_native PROC
    movzx eax, BYTE PTR [rcx]
    ret
cmeta_static_branch_native ENDP

PUBLIC cmeta_static_native_target
cmeta_static_native_target PROC
    mov rax, QWORD PTR [rcx]
    ret
cmeta_static_native_target ENDP
END
