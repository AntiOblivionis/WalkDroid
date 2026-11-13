/* ARM32 pointer/integer-only four-argument boundary. Preserve original Sony
 * callees that clobber d8-d15. Fifth C parameter (native arg3) is on stack. */
.syntax unified
.thumb
.fpu neon
.text
.global sony_audio_native_call4
.type sony_audio_native_call4,%function
.thumb_func
sony_audio_native_call4:
    ldr ip,[sp]
    push {r4,lr}
    vpush {d8-d15}
    mov r4,r0
    mov r0,r1
    mov r1,r2
    mov r2,r3
    mov r3,ip
    blx r4
    vpop {d8-d15}
    pop {r4,pc}
.size sony_audio_native_call4,.-sony_audio_native_call4
.section .note.GNU-stack,"",%progbits
