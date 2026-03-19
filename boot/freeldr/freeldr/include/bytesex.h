#pragma once

static inline VOID __SWAPB(PUCHAR B, ULONG S)
{
    ULONG HalfS = S >> 1;

    for (ULONG i = 0, j = S - 1; i < HalfS; i++, j--)
    {
        UCHAR B1 = B[i];
        UCHAR B2 = B[j];

        B[i] = B2;
        B[j] = B1;
    }
}

#define __SWAPX(Name, Type)                         \
    static inline Type Name(Type Value)             \
    {                                               \
        Type Swapped = Value;                       \
        __SWAPB((PUCHAR)&Swapped, sizeof(Swapped)); \
        return Swapped;                             \
    }

__SWAPX(__SWAPW, USHORT)
__SWAPX(__SWAPD, ULONG)
__SWAPX(__SWAPQ, ULONGLONG)

#ifdef _PPC_
#define SWAPQ(x) __SWAPQ(x)
#define SWAPD(x) __SWAPD(x)
#define SWAPW(x) __SWAPW(x)
#else
#define SWAPQ(x) x
#define SWAPD(x) x
#define SWAPW(x) x
#endif
#define SQ(Object,Field) Object->Field = SWAPQ(Object->Field)
#define SD(Object,Field) Object->Field = SWAPD(Object->Field)
#define SW(Object,Field) Object->Field = SWAPW(Object->Field)
