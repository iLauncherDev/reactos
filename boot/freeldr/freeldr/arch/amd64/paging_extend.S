
#include <asm.inc>

PUBLIC get_cr3

get_cr3:
    mov rax, cr3
    ret
