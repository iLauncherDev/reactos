#pragma once
#include <ntdef.h>

#ifndef wcsnlen

static inline SIZE_T _wcsnlen_inlined(const WCHAR *str, SIZE_T max)
{
    SIZE_T c = 0;
    WCHAR *str_end = (WCHAR *)&str[max];

    while (str < str_end && *str)
        str++, c++;

    return c;
}

#define wcsnlen _wcsnlen_inlined
#endif

#ifndef strnlen

static inline SIZE_T _strnlen_inlined(const CHAR *str, SIZE_T max)
{
    SIZE_T c = 0;
    CHAR *str_end = (CHAR *)&str[max];

    while (str < str_end && *str)
        str++, c++;

    return c;
}

#define strnlen _strnlen_inlined
#endif
