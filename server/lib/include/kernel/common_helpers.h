/**
 * @file common_helpers.h
 * @brief 通用辅助函数头文件

 */

#pragma strict_types

#ifndef COMMON_HELPERS_H
#define COMMON_HELPERS_H

/*
 * 当前仓库没有稳定落地的 `allocate_buffer/trim/starts_with/...` 通用 helper 真源，
 * 因此这里不再继续声明一组“看起来可以直接调用”的函数契约。
 * 本头文件当前只保留路径宏与参考说明，避免把假 API 面继续扩散进新代码。
 */

/* 工具模块路径定义
 * ============================================================ */

/** @brief 公共工具函数路径 */
#define COMMON_UTILS    "/lib/std/common_utils"

/**
 * @brief 银行工具兼容占位路径
 * @note 当前仓库没有稳定的 bank utils 实现文件，本宏保留 0 哨兵防止新代码误把头文件路径当运行时模块路径。
 */
#define BANK_UTILS_PATH 0

#endif /* COMMON_HELPERS_H */
