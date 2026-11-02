/**
 * @file    macros.h
 * @brief   对象路径前缀宏
 *
 * @par Description:
 * 只提供 `__DIR__`：它基于 `file_name(this_object())` 在运行期求值，得到当前对象所在
 * 目录前缀，只用于 `inherit __DIR__ "sibling";` 形态；它不是驱动编译期源文件常量。
 * 数值与集合工具请直接使用 efun/基类方法，不在公共头里另立函数形态的宏。
 */

#ifndef __CORE_MACROS_H__
#define __CORE_MACROS_H__

#define __DIR__     (explode(file_name(this_object()), "/")[0..<2] * "/") + "/"

#endif // __CORE_MACROS_H__
