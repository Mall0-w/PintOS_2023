#ifndef FIXED_POINT_H
#define FIXED_POINT_H

#include <stdint.h>

#define F 16384

static inline
int int_to_fp (int n){
    return n*F;
}

static inline
int fp_to_int_truncated (int x) {
    return x / F;
}

static inline
int fp_to_int_rounded (int x){
    int offset;
    if (x < 0) offset = -(F/2);
    else offset = F/2;
    return (x + offset) / F;
}

static inline
int add_n_to_fp (int n, int x) {
    return x + n * F;
}

static inline
int subtract_n_from_fp(int n, int x) {
    return x - n * F;
}

static inline
int subtract_fp_from_n(int n, int x) {
    return n * F - x;
}

static inline
int multiply_fp(int x, int y){
    return (int) (((int64_t) x) * y /F);
}

static inline
int divide_fp(int x, int y){
    return (int) (((int64_t) x) * F / y);
}

#endif /* threads/fixed-point.h */