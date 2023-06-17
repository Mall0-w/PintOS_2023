#include "threads/fixed-point.h"

int int_to_fp (int n){
    return n*F;
}

int fp_to_int (int n){
    int offset;
    if (n < 0) offset = -(F/2);
    else offset = F/2;
    return (n + offset) / F;
}

int fp_mult(int x, int y){
    return (int) (((int64_t) x) * y /F);
}

int fp_div(int x, int y){
    return (int) (((int64_t) x) * F / y);
}