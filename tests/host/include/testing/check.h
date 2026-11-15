#ifndef DEATHBOOBS_HOST_CHECK_H
#define DEATHBOOBS_HOST_CHECK_H

#include <sys/core.h>

extern _Noreturn void exit(int status);

_Noreturn static inline void check_fail(i32 check)
{
    exit(check);
}

static inline void check_passed(void)
{
}

#define EXPECT(check, condition)                                      \
    do {                                                               \
        _Static_assert((check) >= 1 && (check) <= 255,                 \
                       "check numbers must fit in an exit status");   \
        if (!(condition)) {                                            \
            check_fail(check);                                         \
        }                                                              \
    } while (0)

#endif
