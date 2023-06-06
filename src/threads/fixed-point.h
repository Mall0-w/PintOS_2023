#ifndef FIXED_POINT_H
#define FIXED_POINT_H

#include <stdint.h>

#define F 16384

#define int_to_fp(n) (n*F)

extern inline
int fp_to_int (int n){
    int offset;
    if (n < 0) offset = -(F/2);
    else offset = F/2;
    return (n + offset) / F;
}

extern inline
int multiply_fp(int x, int y){
    return (int) (((int64_t) x) * y /F);
}

extern inline
int divide_fp(int x, int y){
    return (int) (((int64_t) x) * F / y);
}

#endif /* threads/fixed-point.h */