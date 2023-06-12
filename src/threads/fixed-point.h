#ifndef FIXED_POINT_H
#define FIXED_POINT_H

#include <stdint.h>

#define F 16384

extern inline
int64_t int_to_fp (int n){
    return n*F;
}

extern inline
int fp_to_int_truncated (int64_t x) {
    return x / F;
}

extern inline
int fp_to_int_rounded (int64_t x){
    int offset;
    if (x < 0) offset = -(F/2);
    else offset = F/2;
    return (x + offset) / F;
}

extern inline
int64_t multiply_fp(int64_t x, int64_t y){
    return ((int64_t) x) * y / F;
}

extern inline
int64_t divide_fp(int64_t x, int64_t y){
    return ((int64_t) x) * F / y;
}

#endif /* threads/fixed-point.h */