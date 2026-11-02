/**
 * @file    dbase.h
 * @brief   数据存储框架契约头
 * @note    供其他框架文件引用 `kernel/framework/data.c` 当前真实导出接口；本头文件只定义契约，不承载实现。
 */

#pragma strict_types

#ifndef __DBASE_H__
#define __DBASE_H__

int set(string path, mixed value);
mixed query(string path);
int add(string path, int value);
int delete(string prop);

/* 玩家状态 owner 使用的受保护 raw facade；普通业务不得调用。 */
mixed data_owner_query(string path);
mapping data_owner_query_data();
int data_owner_set(string path, mixed value);
int data_owner_delete(string path);
int data_owner_replace(mapping data);
mapping query_data_error();
int query_data_schema_version();

// 临时属性管理
int set_temp(string path, mixed value);
mixed query_temp(string path);
int delete_temp(string prop);

// 默认值与完整数据映射
mixed query_default(string path, mixed default_val);
int set_data(mapping data);
mapping query_data();

// 存档管理
void set_save_path(string path);
string query_save_path();
void set_auto_save(int flag);
void set_auto_save_delay(int seconds);
int query_auto_save_delay();
int save();
int restore();
int is_modified();

// 临时数据辅助
void clear_temp();

// 工具函数
string *dbase_keys();
mixed *dbase_values();
int exists(string path);
void clear();
int dbase_size();

#endif /* __DBASE_H__ */
