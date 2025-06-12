#include "extfs.h"

BOOLEAN ExtfsMatchExpressionA(PCHAR Pattern, PCHAR Name, BOOLEAN CaseInsensitive)
{
    while (*Pattern)
    {
        if (*Pattern == '*')
        {
            Pattern++;
            if (!*Pattern)
                return TRUE;
            while (*Name)
            {
                if (ExtfsMatchExpressionA(Pattern, Name, CaseInsensitive))
                    return TRUE;
                Name++;
            }
            return FALSE;
        }
        else if (*Pattern == '?')
        {
            if (!*Name)
                return FALSE;
            Pattern++;
            Name++;
        }
        else
        {
            if (CaseInsensitive ? (towlower(*Pattern) != towlower(*Name)) : (*Pattern != *Name))
                return FALSE;
            Pattern++;
            Name++;
        }
    }

    return !*Name;
}
