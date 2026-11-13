.syntax unified
.arm
.text
.global __aeabi_ldivmod
.hidden __aeabi_ldivmod
.type __aeabi_ldivmod,%function
__aeabi_ldivmod:
    push {r0-r4,lr}
    mov r2,#1
    b 1f
.global __aeabi_uldivmod
.hidden __aeabi_uldivmod
.type __aeabi_uldivmod,%function
__aeabi_uldivmod:
    push {r0-r4,lr}
    mov r2,#0
1:
    sub sp,sp,#16
    add r1,sp,#16
    mov r0,sp
    bl s40_div64_words
    ldm sp,{r0-r3}
    add sp,sp,#32
    pop {r4,pc}
