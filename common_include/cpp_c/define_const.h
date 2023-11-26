/**
 * @file define_const.h
 * @author Zuoru YANG (zryang@cse.cuhk.edu.hk)
 * @brief common define and const (independent from C/C++)
 * @version 0.1
 * @date 2023-05-13
 *
 * @copyright Copyright (c) 2023
 *
 */

#ifndef OPEN_NOTE_DEFINE_CONST_H
#define OPEN_NOTE_DEFINE_CONST_H

#include <stdint.h>

// length config use MACRO
#define MAX_READ_BUF_SIZE 4096
#define CRYPTO_BLOCK_SIZE 16
#define MAX_HASH_SIZE 32

// unit conversion
#define MiB_2_B (1ULL << 20)
#define KiB_2_B (1ULL << 10)
#define SEC_2_US (1ULL << 20)

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define MIN(a,b) (((a) <= (b)) ? (a) : (b))
#define MAX(a,b) (((a) >= (b)) ? (a) : (b))
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#define BIT_SET(byte, pos) ((byte) |= (1 << (pos)))
#define BIT_CLEAR(byte, pos) ((byte) &= ~(1 << (pos)))

// error code
#ifndef RETURN_OK
#define RETURN_OK 0
#endif
#ifndef RETURN_ERROR
#define RETURN_ERROR 1
#endif

// var status flag
#ifndef IN
#define IN
#endif
#ifndef OUT
#define OUT
#endif

#endif
