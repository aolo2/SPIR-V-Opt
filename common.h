#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;

typedef int64_t s64;
typedef int32_t s32;
typedef int16_t s16;
typedef int8_t  s8;

typedef float  f32;
typedef double f64;

#define ASSERT(expr) {\
    if (!(expr)) {\
        printf("[ASSERT] %s:%d\n", __FILE__, __LINE__);\
        fflush(stdout);\
        exit(1);\
    }\
}

#define ASSERT_VK(result) {\
    typeof(result) _tmp = (result);\
    if (_tmp != VK_SUCCESS) printf("%d\n", _tmp);\
    ASSERT((_tmp) == VK_SUCCESS) \
}


// #include <gsl/gsl_matrix.h>

// gsl_matrix_alloc(w, h)
// gsl_matrix_set(*m, x, y, val)
// gsl_matrix_get(*m, x, y)
// gsl_matrix_free(*m)