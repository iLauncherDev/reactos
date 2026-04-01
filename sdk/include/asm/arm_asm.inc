

#ifdef _USE_ML

#define PUBLIC EXPORT
#define EXTERN IMPORT

/* Explicit radix in MASM syntax  */
#define BIN(x) x##y
#define OCT(x) x##q
#define DEC(x) x##t
#define HEX(x) 0##x##h

#define FUNC(x) x

    MACRO
    TEXTAREA
        AREA |.text|,ALIGN=4,CODE,READONLY
    MEND

#else

#define PUBLIC .global
#define EXTERN .extern

/* Explicit radix in GAS syntax */
#define BIN(x) 0b##x
#define OCT(x) 0##x
#define DEC(x) x
#define HEX(x) 0x##x

#define FUNC(x) x:

#define TEXTAREA \
    .section .text

#define END

#endif

#define call \
    bl
#define ret \
    bx lr
