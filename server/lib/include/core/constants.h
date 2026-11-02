/**
 * @file    constants.h
 * @brief   核心常量定义 - Core Constants



 *
 * @par Description:
 * 定义核心框架使用的通用常量：
 * - 布尔值常量
 * - 特殊值常量
 * - 通用状态常量
 *
 * @par Usage:
 * #include <core/constants.h>
 */

#pragma strict_types

#ifndef __CORE_CONSTANTS_H__
#define __CORE_CONSTANTS_H__


#ifndef TRUE
#define TRUE    1
#endif

#ifndef FALSE
#define FALSE   0
#endif


#define NULL        0
#define UNDEFINED   -1
#define UNLIMITED   -2
#define ALL         -3


#define STATE_OK        0
#define STATE_ERROR     1
#define STATE_PENDING   2
#define STATE_BUSY      3


#define RESULT_SUCCESS  1
#define RESULT_FAIL     0
#define RESULT_ERROR    -1

#endif // __CORE_CONSTANTS_H__
