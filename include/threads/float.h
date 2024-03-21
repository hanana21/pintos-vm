#include <debug.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define f 1 << 14

/* 
 * x and y are fixed-point numbers, n is an integer
 * fixed-point numbers are in signed p.q format where p + q = 31, and f is 1 << q
*/

const int convert_ntox (const int n);
const int convert_xton (const int x);

const int add_xandn (const int x, const int n);
const int add_xandy (const int x, const int y);

const int sub_nfromx (const int x, const int n);
const int sub_yfromx (const int x, const int y);

const int mult_xbyn (const int x, const int n);
const int mult_xbyy (const int x, const int y);

const int divide_xbyn (const int x, const int n);
const int divide_xbyy (const int x, const int y);