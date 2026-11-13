/* Five integer/pointer arguments. Preserve native-clobbered VFP registers. */
.syntax unified
.thumb
.fpu neon
.text
.global sony_audio_native_call5
.type sony_audio_native_call5,%function
.thumb_func
sony_audio_native_call5:
    push {r4,lr}
    vpush {d8-d15}
    sub sp,sp,#8
    mov r4,r0
    mov r0,r1
    mov r1,r2
    mov r2,r3
    ldr r3,[sp,#80]
    ldr ip,[sp,#84]
    str ip,[sp]
    blx r4
    add sp,sp,#8
    vpop {d8-d15}
    pop {r4,pc}
.size sony_audio_native_call5,.-sony_audio_native_call5
.section .note.GNU-stack,"",%progbits
