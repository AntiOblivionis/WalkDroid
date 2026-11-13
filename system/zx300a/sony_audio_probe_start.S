.syntax unified
.arm
.global _start
.type _start, %function
_start:
    mov fp, #0
    mov lr, #0
    mov ip, sp
    ldr r1, [sp]
    add r2, sp, #4
    mov r3, #0
    sub sp, sp, #16
    str r3, [sp]
    str r3, [sp, #4]
    str ip, [sp, #8]
    ldr r0, =main
    bl __libc_start_main
1:  b 1b
.section .note.GNU-stack,"",%progbits
