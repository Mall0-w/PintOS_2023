#ifndef FIXED_POINT_H
#define FIXED_POINT_H

#include <stdint.h>

#define F 16384

int int_to_fp (int n);

int fp_to_int (int n);

int fp_mult(int x, int y);

int fp_div(int x, int y);

#endif /* threads/fixed-point.h */