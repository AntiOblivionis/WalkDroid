/* ARM32 integer/pointer-only Sony call boundary. Some original call paths
 * overwrite d8-d15; preserve the AAPCS caller's callee-saved VFP state here.
 * r0=function, r1/r2/r3=arguments 0/1/2; returns one word in r0.
 * Not suitable for floating-point arguments, sret aggregates, or >3 arguments.
 */
.syntax unified
.thumb
.fpu neon
.text
.global sony_audio_native_call3
.type sony_audio_native_call3,%function
.thumb_func
sony_audio_native_call3:
    push {r4,lr}
#ifndef SONY_NATIVE_ABI_NEGATIVE_CONTROL
    vpush {d8-d15}
#endif
    mov r4,r0
    mov r0,r1
    mov r1,r2
    mov r2,r3
    movs r3,#0
    blx r4
#ifndef SONY_NATIVE_ABI_NEGATIVE_CONTROL
    vpop {d8-d15}
#endif
    pop {r4,pc}
.size sony_audio_native_call3,.-sony_audio_native_call3
.section .note.GNU-stack,"",%progbits
