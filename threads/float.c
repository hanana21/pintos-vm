#include <stdint.h>
#include <debug.h>
#include <stdbool.h>
#include <stddef.h>
#include "threads/float.h"

const int convert_ntox (const int n) {
    // TODO: implement four base operations
   return n*f;
}
const int convert_xton (const int x) {
    // TODO: implement four base operations
    // rounding to nearest
    if (x>=0) return (x+f/2)/f;
    else if(x<=0) return (x-f/2)/f;
    else return x/f;
}

const int add_xandn (const int x, const int n) {
    // TODO: implement four base operations
    return x+n*f;
}
const int add_xandy (const int x, const int y) {
    // TODO: implement four base operations
    return x+y;
}

const int sub_nfromx (const int x, const int n) {
    // TODO: implement four base operations
    return x-n*f;
}
const int sub_yfromx (const int x, const int y) {
    // TODO: implement four base operations
    return x-y;
}

const int mult_xbyn (const int x, const int n) {
    // TODO: implement four base operations
    return x * n;
}
const int mult_xbyy (const int x, const int y) {
    // TODO: implement four base operations
    return ((int64_t)x)*y/f;
}

const int divide_xbyn (const int x, const int n) {
    // TODO: implement four base operations
    return x/n;
}
const int divide_xbyy (const int x, const int y) {
    // TODO: implement four base operations  
    return ((int64_t)x)*f/y;
}