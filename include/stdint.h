/* ============================================================================
 * include/stdint.h — 基本整数类型
 *
 * 内核不能用系统头文件 (-nostdinc), 需要自己定义基础类型。
 * ============================================================================ */

#ifndef _STDINT_H
#define _STDINT_H

/* 精确宽度整数 */
typedef unsigned char       uint8_t;
typedef signed char         int8_t;
typedef unsigned short      uint16_t;
typedef signed short        int16_t;
typedef unsigned int        uint32_t;
typedef signed int          int32_t;
typedef unsigned long long  uint64_t;
typedef signed long long    int64_t;

/* 指针宽度整数 */
typedef uint32_t            uintptr_t;
typedef int32_t             intptr_t;

/* size_t */
typedef uint32_t            size_t;

/* NULL */
#define NULL ((void *)0)

/* offsetof */
#define offsetof(type, member) __builtin_offsetof(type, member)

#endif /* _STDINT_H */
