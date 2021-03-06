/**
 * Copyright (c) 2013-2014 Tomas Dzetkulic
 * Copyright (c) 2013-2014 Pavol Rusnak
 * Copyright (c)      2015 Jochen Hoenicke
 * Copyright (c)      2016 Alex Beregszaszi
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
 * OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "bignum.h"
#include "memzero.h"

/* big number library */

/* The structure bignum256 is an array of nine 32-bit values, which
 * are digits in base 2^30 representation.  I.e. the number
 *   bignum256 a;
 * represents the value
 *   sum_{i=0}^8 a.val[i] * 2^{30 i}.
 *
 * The number is *normalized* iff every digit is < 2^30.
 *
 * As the name suggests, a bignum256 is intended to represent a 256
 * bit number, but it can represent 270 bits.  Numbers are usually
 * reduced using a prime, either the group order or the field prime.
 * The reduction is often partly done by bn_fast_mod, and similarly
 * implicitly in bn_multiply.  A *partly reduced number* is a
 * normalized number between 0 (inclusive) and 2*prime (exclusive).
 *
 * A partly reduced number can be fully reduced by calling bn_mod.
 * Only a fully reduced number is guaranteed to fit in 256 bit.
 *
 * All functions assume that the prime in question is slightly smaller
 * than 2^256.  In particular it must be between 2^256-2^224 and
 * 2^256 and it must be a prime number.
 */

inline uint32_t read_be(const uint8_t *data)
{
	return (((uint32_t)data[0]) << 24) |
	       (((uint32_t)data[1]) << 16) |
	       (((uint32_t)data[2]) << 8)  |
	       (((uint32_t)data[3]));
}

inline void write_be(uint8_t *data, uint32_t x)
{
	data[0] = x >> 24;
	data[1] = x >> 16;
	data[2] = x >> 8;
	data[3] = x;
}

inline uint32_t read_le(const uint8_t *data)
{
	return (((uint32_t)data[3]) << 24) |
	       (((uint32_t)data[2]) << 16) |
	       (((uint32_t)data[1]) << 8)  |
	       (((uint32_t)data[0]));
}

inline void write_le(uint8_t *data, uint32_t x)
{
	data[3] = x >> 24;
	data[2] = x >> 16;
	data[1] = x >> 8;
	data[0] = x;
}

// convert a raw bigendian 256 bit value into a normalized bignum.
// out_number is partly reduced (since it fits in 256 bit).
void bn_read_be(const uint8_t *in_number, bignum256 *out_number)
{
	int i;
	uint32_t temp = 0;
	for (i = 0; i < 8; i++) {
		// invariant: temp = (in_number % 2^(32i)) >> 30i
		// get next limb = (in_number % 2^(32(i+1))) >> 32i
		uint32_t limb = read_be(in_number + (7 - i) * 4);
		// temp = (in_number % 2^(32(i+1))) << 30i
		temp |= limb << (2*i);
		// store 30 bits into val[i]
		out_number->val[i]= temp & 0x3FFFFFFF;
		// prepare temp for next round
		temp = limb >> (30 - 2*i);
	}
	out_number->val[8] = temp;
}

// convert a normalized bignum to a raw bigendian 256 bit number.
// in_number must be fully reduced.
void bn_write_be(const bignum256 *in_number, uint8_t *out_number)
{
	int i;
	uint32_t temp = in_number->val[8];
	for (i = 0; i < 8; i++) {
		// invariant: temp = (in_number >> 30*(8-i))
		uint32_t limb = in_number->val[7 - i];
		temp = (temp << (16 + 2*i)) | (limb >> (14 - 2*i));
		write_be(out_number + i * 4, temp);
		temp = limb;
	}
}

// convert a raw little endian 256 bit value into a normalized bignum.
// out_number is partly reduced (since it fits in 256 bit).
void bn_read_le(const uint8_t *in_number, bignum256 *out_number)
{
	int i;
	uint32_t temp = 0;
	for (i = 0; i < 8; i++) {
		// invariant: temp = (in_number % 2^(32i)) >> 30i
		// get next limb = (in_number % 2^(32(i+1))) >> 32i
		uint32_t limb = read_le(in_number + i * 4);
		// temp = (in_number % 2^(32(i+1))) << 30i
		temp |= limb << (2*i);
		// store 30 bits into val[i]
		out_number->val[i]= temp & 0x3FFFFFFF;
		// prepare temp for next round
		temp = limb >> (30 - 2*i);
	}
	out_number->val[8] = temp;
}

// convert a normalized bignum to a raw little endian 256 bit number.
// in_number must be fully reduced.
void bn_write_le(const bignum256 *in_number, uint8_t *out_number)
{
	int i;
	uint32_t temp = in_number->val[8];
	for (i = 0; i < 8; i++) {
		// invariant: temp = (in_number >> 30*(8-i))
		uint32_t limb = in_number->val[7 - i];
		temp = (temp << (16 + 2*i)) | (limb >> (14 - 2*i));
		write_le(out_number + (7 - i) * 4, temp);
		temp = limb;
	}
}

void bn_read_uint32(uint32_t in_number, bignum256 *out_number)
{
	out_number->val[0] = in_number & 0x3FFFFFFF;
	out_number->val[1] = in_number >> 30;
	out_number->val[2] = 0;
	out_number->val[3] = 0;
	out_number->val[4] = 0;
	out_number->val[5] = 0;
	out_number->val[6] = 0;
	out_number->val[7] = 0;
	out_number->val[8] = 0;
}

void bn_read_uint64(uint64_t in_number, bignum256 *out_number)
{
	out_number->val[0] = in_number & 0x3FFFFFFF;
	out_number->val[1] = (in_number >>= 30) & 0x3FFFFFFF;
	out_number->val[2] = in_number >>= 30;
	out_number->val[3] = 0;
	out_number->val[4] = 0;
	out_number->val[5] = 0;
	out_number->val[6] = 0;
	out_number->val[7] = 0;
	out_number->val[8] = 0;
}

// a must be normalized
int bn_bitcount(const bignum256 *a)
{
	int i;
	for (i = 8; i >= 0; i--) {
		int tmp = a->val[i];
		if (tmp != 0) {
			return i * 30 + (32 - __builtin_clz(tmp));
		}
	}
	return 0;
}

#define DIGITS 78 // log10(2 ^ 256)

unsigned int bn_digitcount(const bignum256 *a)
{
	bignum256 val;
	memcpy(&val, a, sizeof(bignum256));

	unsigned int digits = 1;

	for (unsigned int i = 0; i < DIGITS; i += 3) {
		uint32_t limb;
		bn_divmod1000(&val, &limb);

		if (limb >= 100) {
			digits = i + 3;
		} else if (limb >= 10) {
			digits = i + 2;
		} else if (limb >= 1) {
			digits = i + 1;
		}
	}

	return digits;
}

// sets a bignum to zero.
void bn_zero(bignum256 *a)
{
	int i;
	for (i = 0; i < 9; i++) {
		a->val[i] = 0;
	}
}

// sets a bignum to one.
void bn_one(bignum256 *a)
{
	a->val[0] = 1;
	a->val[1] = 0;
	a->val[2] = 0;
	a->val[3] = 0;
	a->val[4] = 0;
	a->val[5] = 0;
	a->val[6] = 0;
	a->val[7] = 0;
	a->val[8] = 0;
}

// checks that a bignum is zero.
// a must be normalized
// function is constant time (on some architectures, in particular ARM).
int bn_is_zero(const bignum256 *a)
{
	int i;
	uint32_t result = 0;
	for (i = 0; i < 9; i++) {
		result |= a->val[i];
	}
	return !result;
}

// Check whether a < b
// a and b must be normalized
// function is constant time (on some architectures, in particular ARM).
int bn_is_less(const bignum256 *a, const bignum256 *b)
{
	int i;
	uint32_t res1 = 0;
	uint32_t res2 = 0;
	for (i = 8; i >= 0; i--) {
		res1 = (res1 << 1) | (a->val[i] < b->val[i]);
		res2 = (res2 << 1) | (a->val[i] > b->val[i]);
	}
	return res1 > res2;
}

// Check whether a == b
// a and b must be normalized
// function is constant time (on some architectures, in particular ARM).
int bn_is_equal(const bignum256 *a, const bignum256 *b) {
	int i;
	uint32_t result = 0;
	for (i = 0; i < 9; i++) {
		result |= (a->val[i] ^ b->val[i]);
	}
	return !result;
}

// Assigns res = cond ? truecase : falsecase
// assumes that cond is either 0 or 1.
// function is constant time.
void bn_cmov(bignum256 *res, int cond, const bignum256 *truecase, const bignum256 *falsecase)
{
	int i;
	uint32_t tmask = (uint32_t) -cond;
	uint32_t fmask = ~tmask;

	assert (cond == 1 || cond == 0);
	for (i = 0; i < 9; i++) {
		res->val[i] = (truecase->val[i] & tmask) |
			(falsecase->val[i] & fmask);
	}
}

// shift number to the left, i.e multiply it by 2.
// a must be normalized.  The result is normalized but not reduced.
void bn_lshift(bignum256 *a)
{
	int i;
	for (i = 8; i > 0; i--) {
		a->val[i] = ((a->val[i] << 1) & 0x3FFFFFFF) | ((a->val[i - 1] & 0x20000000) >> 29);
	}
	a->val[0] = (a->val[0] << 1) & 0x3FFFFFFF;
}

// shift number to the right, i.e divide by 2 while rounding down.
// a must be normalized.  The result is normalized.
void bn_rshift(bignum256 *a)
{
	int i;
	for (i = 0; i < 8; i++) {
		a->val[i] = (a->val[i] >> 1) | ((a->val[i + 1] & 1) << 29);
	}
	a->val[8] >>= 1;
}

// sets bit in bignum
void bn_setbit(bignum256 *a, uint8_t bit)
{
	a->val[bit / 30] |= (1u << (bit % 30));
}

// clears bit in bignum
void bn_clearbit(bignum256 *a, uint8_t bit)
{
	a->val[bit / 30] &= ~(1u << (bit % 30));
}

// tests bit in bignum
uint32_t bn_testbit(bignum256 *a, uint8_t bit)
{
	return a->val[bit / 30] & (1u << (bit % 30));
}

// a = b ^ c
void bn_xor(bignum256 *a, const bignum256 *b, const bignum256 *c)
{
	int i;
	for (i = 0; i < 9; i++) {
		a->val[i] = b->val[i] ^ c->val[i];
	}
}

// multiply x by 1/2 modulo prime.
// it computes x = (x & 1) ? (x + prime) >> 1 : x >> 1.
// assumes x is normalized.
// if x was partly reduced, it is also partly reduced on exit.
// function is constant time.
void bn_mult_half(bignum256 * x, const bignum256 *prime)
{
	int j;
	uint32_t xodd = -(x->val[0] & 1);
	// compute x = x/2 mod prime
	// if x is odd compute (x+prime)/2
	uint32_t tmp1 = (x->val[0] + (prime->val[0] & xodd)) >> 1;
	for (j = 0; j < 8; j++) {
		uint32_t tmp2 = (x->val[j+1] + (prime->val[j+1] & xodd));
		tmp1 += (tmp2 & 1) << 29;
		x->val[j] = tmp1 & 0x3fffffff;
		tmp1 >>= 30;
		tmp1 += tmp2 >> 1;
	}
	x->val[8] = tmp1;
}

// multiply x by k modulo prime.
// assumes x is normalized, 0 <= k <= 4.
// guarantees x is partly reduced.
void bn_mult_k(bignum256 *x, uint8_t k, const bignum256 *prime)
{
	int j;
	for (j = 0; j < 9; j++) {
		x->val[j] = k * x->val[j];
	}
	bn_fast_mod(x, prime);
}

// compute x = x mod prime  by computing  x >= prime ? x - prime : x.
// assumes x partly reduced, guarantees x fully reduced.
void bn_mod(bignum256 *x, const bignum256 *prime)
{
	const int flag = bn_is_less(x, prime); // x < prime
	bignum256 temp;
	bn_subtract(x, prime, &temp); // temp = x - prime
	bn_cmov(x, flag, x, &temp);
}

// auxiliary function for multiplication.
// compute k * x as a 540 bit number in base 2^30 (normalized).
// assumes that k and x are normalized.
void bn_multiply_long(const bignum256 *k, const bignum256 *x, uint32_t res[18])
{
	int i, j;
	uint64_t temp = 0;

	// compute lower half of long multiplication
	for (i = 0; i < 9; i++)
	{
		for (j = 0; j <= i; j++) {
			// no overflow, since 9*2^60 < 2^64
			temp += k->val[j] * (uint64_t)x->val[i - j];
		}
		res[i] = temp & 0x3FFFFFFFu;
		temp >>= 30;
	}
	// compute upper half
	for (; i < 17; i++)
	{
		for (j = i - 8; j < 9 ; j++) {
			// no overflow, since 9*2^60 < 2^64
			temp += k->val[j] * (uint64_t)x->val[i - j];
		}
		res[i] = temp & 0x3FFFFFFFu;
		temp >>= 30;
	}
	res[17] = temp;
}

// auxiliary function for multiplication.
// reduces res modulo prime.
// assumes    res normalized, res < 2^(30(i-7)) * 2 * prime
// guarantees res normalized, res < 2^(30(i-8)) * 2 * prime
void bn_multiply_reduce_step(uint32_t res[18], const bignum256 *prime, uint32_t i) {
	// let k = i-8.
	// on entry:
	//   0 <= res < 2^(30k + 31) * prime
	// estimate coef = (res / prime / 2^30k)
	// by coef = res / 2^(30k + 256)  rounded down
	// 0 <= coef < 2^31
	// subtract (coef * 2^(30k) * prime) from res
	// note that we unrolled the first iteration
	uint32_t j;
	uint32_t coef = (res[i] >> 16) + (res[i + 1] << 14);
	uint64_t temp = 0x2000000000000000ull + res[i - 8] - prime->val[0] * (uint64_t)coef;
	assert (coef < 0x80000000u);
	res[i - 8] = temp & 0x3FFFFFFF;
	for (j = 1; j < 9; j++) {
		temp >>= 30;
		// Note: coeff * prime->val[j] <= (2^31-1) * (2^30-1)
		// Hence, this addition will not underflow.
		temp += 0x1FFFFFFF80000000ull + res[i - 8 + j] - prime->val[j] * (uint64_t)coef;
		res[i - 8 + j] = temp & 0x3FFFFFFF;
		// 0 <= temp < 2^61 + 2^30
	}
	temp >>= 30;
	temp += 0x1FFFFFFF80000000ull + res[i - 8 + j];
	res[i - 8 + j] = temp & 0x3FFFFFFF;
	// we rely on the fact that prime > 2^256 - 2^224
	//   res = oldres - coef*2^(30k) * prime;
	// and
	//   coef * 2^(30k + 256) <= oldres < (coef+1) * 2^(30k + 256)
	// Hence, 0 <= res < 2^30k (2^256 + coef * (2^256 - prime))
	//                 < 2^30k (2^256 + 2^31 * 2^224)
	//                 < 2^30k (2 * prime)
}


// auxiliary function for multiplication.
// reduces x = res modulo prime.
// assumes    res normalized, res < 2^270 * 2 * prime
// guarantees x partly reduced, i.e., x < 2 * prime
void bn_multiply_reduce(bignum256 *x, uint32_t res[18], const bignum256 *prime)
{
	int i;
	// res = k * x is a normalized number (every limb < 2^30)
	// 0 <= res < 2^270 * 2 * prime.
	for (i = 16; i >= 8; i--) {
		bn_multiply_reduce_step(res, prime, i);
		assert(res[i + 1] == 0);
	}
	// store the result
	for (i = 0; i < 9; i++) {
		x->val[i] = res[i];
	}
}

// Compute x := k * x  (mod prime)
// both inputs must be smaller than 180 * prime.
// result is partly reduced (0 <= x < 2 * prime)
// This only works for primes between 2^256-2^224 and 2^256.
void bn_multiply(const bignum256 *k, bignum256 *x, const bignum256 *prime)
{
	uint32_t res[18] = {0};
	bn_multiply_long(k, x, res);
	bn_multiply_reduce(x, res, prime); 
	memzero(res, sizeof(res));
}

// partly reduce x modulo prime
// input x does not have to be normalized.
// x can be any number that fits.
// prime must be between (2^256 - 2^224) and 2^256
// result is partly reduced, smaller than 2*prime
void bn_fast_mod(bignum256 *x, const bignum256 *prime)
{
	int j;
	uint32_t coef;
	uint64_t temp;

	coef = x->val[8] >> 16;
	// substract (coef * prime) from x
	// note that we unrolled the first iteration
	temp = 0x2000000000000000ull + x->val[0] - prime->val[0] * (uint64_t)coef;
	x->val[0] = temp & 0x3FFFFFFF;
	for (j = 1; j < 9; j++) {
		temp >>= 30;
		temp += 0x1FFFFFFF80000000ull + x->val[j] - prime->val[j] * (uint64_t)coef;
		x->val[j] = temp & 0x3FFFFFFF;
	}
}

// square root of x = x^((p+1)/4)
// http://en.wikipedia.org/wiki/Quadratic_residue#Prime_or_prime_power_modulus
// assumes    x is normalized but not necessarily reduced.
// guarantees x is reduced
void bn_sqrt(bignum256 *x, const bignum256 *prime)
{
	// this method compute x^1/2 = x^(prime+1)/4
	uint32_t i, j, limb;
	bignum256 res, p;
	bn_one(&res);
	// compute p = (prime+1)/4
	memcpy(&p, prime, sizeof(bignum256));
	bn_addi(&p, 1);
	bn_rshift(&p);
	bn_rshift(&p);
	for (i = 0; i < 9; i++) {
		// invariants:
		//    x   = old(x)^(2^(i*30))
		//    res = old(x)^(p % 2^(i*30))
		// get the i-th limb of prime - 2
		limb = p.val[i];
		for (j = 0; j < 30; j++) {
			// invariants:
			//    x    = old(x)^(2^(i*30+j))
			//    res  = old(x)^(p % 2^(i*30+j))
			//    limb = (p % 2^(i*30+30)) / 2^(i*30+j)
			if (i == 8 && limb == 0) break;
			if (limb & 1) {
				bn_multiply(x, &res, prime);
			}
			limb >>= 1;
			bn_multiply(x, x, prime);
		}
	}
	bn_mod(&res, prime);
	memcpy(x, &res, sizeof(bignum256));
	memzero(&res, sizeof(res));
	memzero(&p, sizeof(p));
}

// in field G_prime, small but slow
void bn_inverse(bignum256 *x, const bignum256 *prime)
{
	// this method compute x^-1 = x^(prime-2)
	uint32_t i, j, limb;
	bignum256 res;
	bn_one(&res);
	for (i = 0; i < 9; i++) {
		// invariants:
		//    x   = old(x)^(2^(i*30))
		//    res = old(x)^((prime-2) % 2^(i*30))
		// get the i-th limb of prime - 2
		limb = prime->val[i];
		// this is not enough in general but fine for secp256k1 & nist256p1 because prime->val[0] > 1
		if (i == 0) limb -= 2;
		for (j = 0; j < 30; j++) {
			// invariants:
			//    x    = old(x)^(2^(i*30+j))
			//    res  = old(x)^((prime-2) % 2^(i*30+j))
			//    limb = ((prime-2) % 2^(i*30+30)) / 2^(i*30+j)
			// early abort when only zero bits follow
			if (i == 8 && limb == 0) break;
			if (limb & 1) {
				bn_multiply(x, &res, prime);
			}
			limb >>= 1;
			bn_multiply(x, x, prime);
		}
	}
	bn_mod(&res, prime);
	memcpy(x, &res, sizeof(bignum256));
}

void bn_normalize(bignum256 *a) {
	bn_addi(a, 0);
}

// add two numbers a = a + b
// assumes that a, b are normalized
// guarantees that a is normalized
void bn_add(bignum256 *a, const bignum256 *b)
{
	int i;
	uint32_t tmp = 0;
	for (i = 0; i < 9; i++) {
		tmp += a->val[i] + b->val[i];
		a->val[i] = tmp & 0x3FFFFFFF;
		tmp >>= 30;
	}
}

void bn_addmod(bignum256 *a, const bignum256 *b, const bignum256 *prime)
{
	int i;
	for (i = 0; i < 9; i++) {
		a->val[i] += b->val[i];
	}
	bn_fast_mod(a, prime);
}

void bn_addi(bignum256 *a, uint32_t b) {
	int i;
	uint32_t tmp = b;
	for (i = 0; i < 9; i++) {
		tmp += a->val[i];
		a->val[i] = tmp & 0x3FFFFFFF;
		tmp >>= 30;
	}
}

void bn_subi(bignum256 *a, uint32_t b, const bignum256 *prime) {
	assert (b <= prime->val[0]);
	// the possible underflow will be taken care of when adding the prime
	a->val[0] -= b;
	bn_add(a, prime);
}

// res = a - b mod prime.  More exactly res = a + (2*prime - b).
// b must be a partly reduced number
// result is normalized but not reduced.
void bn_subtractmod(const bignum256 *a, const bignum256 *b, bignum256 *res, const bignum256 *prime)
{
	int i;
	uint32_t temp = 1;
	for (i = 0; i < 9; i++) {
		temp += 0x3FFFFFFF + a->val[i] + 2u * prime->val[i] - b->val[i];
		res->val[i] = temp & 0x3FFFFFFF;
		temp >>= 30;
	}
}

// res = a - b ; a > b
void bn_subtract(const bignum256 *a, const bignum256 *b, bignum256 *res)
{
	int i;
	uint32_t tmp = 1;
	for (i = 0; i < 9; i++) {
		tmp += 0x3FFFFFFF + a->val[i] - b->val[i];
		res->val[i] = tmp & 0x3FFFFFFF;
		tmp >>= 30;
	}
}

// a / 58 = a (+r)
void bn_divmod58(bignum256 *a, uint32_t *r)
{
	int i;
	uint32_t rem, tmp;
	rem = a->val[8] % 58;
	a->val[8] /= 58;
	for (i = 7; i >= 0; i--) {
		// invariants:
		//   rem = old(a) >> 30(i+1) % 58
		//   a[i+1..8] = old(a[i+1..8])/58
		//   a[0..i]   = old(a[0..i])
		// 2^30 == 18512790*58 + 4
		tmp = rem * 4 + a->val[i];
		// set a[i] = (rem * 2^30 + a[i])/58
		//          = rem * 18512790 + (rem * 4 + a[i])/58
		a->val[i] = rem * 18512790 + (tmp / 58);
		// set rem = (rem * 2^30 + a[i]) mod 58
		//         = (rem * 4 + a[i]) mod 58
		rem = tmp % 58;
	}
	*r = rem;
}

// a / 1000 = a (+r)
void bn_divmod1000(bignum256 *a, uint32_t *r)
{
	int i;
	uint32_t rem, tmp;
	rem = a->val[8] % 1000;
	a->val[8] /= 1000;
	for (i = 7; i >= 0; i--) {
		// invariants:
		//   rem = old(a) >> 30(i+1) % 1000
		//   a[i+1..8] = old(a[i+1..8])/1000
		//   a[0..i]   = old(a[0..i])
		// 2^30 == 1073741*1000 + 824
		tmp = rem * 824 + a->val[i];
		// set a[i] = (rem * 2^30 + a[i])/1000
		//          = rem * 1073741 + (rem * 824 + a[i])/1000
		a->val[i] = rem * 1073741 + (tmp / 1000);
		// set rem = (rem * 2^30 + a[i]) mod 1000
		//         = (rem * 824 + a[i]) mod 1000
		rem = tmp % 1000;
	}
	*r = rem;
}

size_t bn_format(const bignum256 *amnt, const char *prefix, const char *suffix, unsigned int decimals, int exponent, bool trailing, char *out, size_t outlen)
{
	size_t prefixlen = prefix ? strlen(prefix) : 0;
	size_t suffixlen = suffix ? strlen(suffix) : 0;

	/* add prefix to beginning of out buffer */
	if (prefixlen) {
		memcpy(out, prefix, prefixlen);
	}
	/* add suffix to end of out buffer */
	if (suffixlen) {
		memcpy(&out[outlen - suffixlen - 1], suffix, suffixlen);
	}
	/* nul terminate (even if suffix = NULL) */
	out[outlen - 1] = '\0';

	/* fill number between prefix and suffix (between start and end) */
	char *start = &out[prefixlen], *end = &out[outlen - suffixlen - 1];
	char *str = end;

#define BN_FORMAT_PUSH_CHECKED(c) \
	do { \
		if (str == start) return 0; \
		*--str = (c); \
	} while (0)

#define BN_FORMAT_PUSH(n) \
	do { \
		if (exponent < 0) { \
			exponent++; \
		} else { \
			if ((n) > 0 || trailing || str != end || decimals <= 1) { \
				BN_FORMAT_PUSH_CHECKED('0' + (n)); \
			} \
			if (decimals > 0 && decimals-- == 1) { \
				BN_FORMAT_PUSH_CHECKED('.'); \
			} \
		} \
	} while (0)

	bignum256 val;
	memcpy(&val, amnt, sizeof(bignum256));

	if (bn_is_zero(&val)) {
		exponent = 0;
	}

	for (; exponent > 0; exponent--) {
		BN_FORMAT_PUSH(0);
	}

	unsigned int digits = bn_digitcount(&val);
	for (unsigned int i = 0; i < digits / 3; i++) {
		uint32_t limb;
		bn_divmod1000(&val, &limb);

		BN_FORMAT_PUSH(limb % 10);
		limb /= 10;
		BN_FORMAT_PUSH(limb % 10);
		limb /= 10;
		BN_FORMAT_PUSH(limb % 10);
	}

	if (digits % 3 != 0) {
		uint32_t limb;
		bn_divmod1000(&val, &limb);

		switch (digits % 3) {
		case 2:
			BN_FORMAT_PUSH(limb % 10);
			limb /= 10;
			//-fallthrough

		case 1:
			BN_FORMAT_PUSH(limb % 10);
			break;
		}
	}

	while (decimals > 0 || str[0] == '\0' || str[0] == '.') {
		BN_FORMAT_PUSH(0);
	}

	/* finally move number to &out[prefixlen] to close the gap between
	 * prefix and str.  len is length of number + suffix + traling 0
	 */
	size_t len = &out[outlen] - str;
	memmove(&out[prefixlen], str, len);

	/* return length of number including prefix and suffix without trailing 0 */
	return prefixlen + len - 1;
}
