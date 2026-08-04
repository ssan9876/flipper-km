#pragma once
#include <stdio.h>
#include <string.h>

static int km_tests_run = 0;
static int km_tests_failed = 0;

#define CHECK(cond, msg)                                             \
    do {                                                             \
        if(!(cond)) {                                                \
            printf("  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
            km_tests_failed++;                                       \
            return;                                                  \
        }                                                            \
    } while(0)

#define RUN(test)                                       \
    do {                                                \
        printf("RUN  %s\n", #test);                     \
        km_tests_run++;                                 \
        int before = km_tests_failed;                   \
        test();                                         \
        if(before == km_tests_failed) printf("  ok\n"); \
    } while(0)

#define REPORT()                                                      \
    do {                                                              \
        printf("\n%d run, %d failed\n", km_tests_run, km_tests_failed); \
        return km_tests_failed == 0 ? 0 : 1;                          \
    } while(0)
