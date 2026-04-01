/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     arm assembly UEFI escape helper functions
 * COPYRIGHT:   Copyright 2023 Justin Miller <justinmiller100@gmail.com>
 */

#include <arm_asm.inc>

    TEXTAREA

    EXTERN UefiServiceStack
    EXTERN ExecuteLoaderCleanly
    EXTERN UefiExitBootServices
    EXTERN BasicStack

    PUBLIC _changestack
FUNC(_changestack)
    mov r0, sp
    ldr r1, =BasicStack
    ldr sp, [r1]

    call ExecuteLoaderCleanly
    ret

    PUBLIC _exituefi
FUNC(_exituefi)
    push {r4, r5}

    mov r4, sp
    ldr r5, =UefiServiceStack
    ldr sp, [r5]

    push {r4}
    call UefiExitBootServices
    pop {r4}

    mov sp, r4

    pop {r4, r5}

    ret

    END
