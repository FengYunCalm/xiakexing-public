/**
 * @file    data_module.h
 * @brief   数据模块头文件 - Data Module Header



 */

#pragma strict_types

#ifndef __DATA_MODULE_H__
#define __DATA_MODULE_H__

/* 数据模块路径定义
 * ============================================================================
 * 当前仓库未提供独立 data_listener/data_indexer 模块实现，
 * 以 0 作为哨兵，调用方必须判空。
 */

#define DATA_LISTENER_MODULE    0
#define DATA_INDEXER_MODULE     0

/* 索引常量
 * ============================================================================ */

#define MAX_INDEX_SIZE      10000
#define INDEX_CACHE_SIZE    100

#endif /* __DATA_MODULE_H__ */
