/**
 * @file    ansi.h
 * @brief   ANSI颜色代码 - ANSI Color Codes



 *
 * @par Description:
 * 定义终端ANSI颜色代码：
 * - 标准颜色
 * - 高亮颜色
 * - 背景颜色
 * - 控制代码
 *
 * @par Usage:
 * #include <system/ansi.h>
 * write(HIR "红色文字" NOR "\n");
 */

#pragma strict_types

#ifndef __GAME_ANSI_H__
#define __GAME_ANSI_H__


#define BLK "\x1b[30m"      // 黑色
#define RED "\x1b[31m"      // 红色
#define GRN "\x1b[32m"      // 绿色
#define YEL "\x1b[33m"      // 黄色
#define BLU "\x1b[34m"      // 蓝色
#define MAG "\x1b[35m"      // 洋红
#define CYN "\x1b[36m"      // 青色
#define WHT "\x1b[37m"      // 白色


#define HIR "\x1b[1;31m"    // 高亮红
#define HIG "\x1b[1;32m"    // 高亮绿
#define HIY "\x1b[1;33m"    // 高亮黄
#define HIB "\x1b[1;34m"    // 高亮蓝
#define HIM "\x1b[1;35m"    // 高亮洋红
#define HIC "\x1b[1;36m"    // 高亮青
#define HIW "\x1b[1;37m"    // 高亮白


#define BRED "\x1b[41m"     // 红色背景
#define BGRN "\x1b[42m"     // 绿色背景
#define BYEL "\x1b[43m"     // 黄色背景
#define BBLU "\x1b[44m"     // 蓝色背景
#define BMAG "\x1b[45m"     // 洋红背景
#define BCYN "\x1b[46m"     // 青色背景
#define BWHT "\x1b[47m"     // 白色背景


#define NOR "\x1b[0m"       // 重置所有属性
#define BOLD "\x1b[1m"      // 粗体
#define CLR "\x1b[2J"       // 清屏


#define COLOR_MSG(msg, color)   (color + msg + NOR)
#define RED_MSG(msg)            (HIR + msg + NOR)
#define GREEN_MSG(msg)          (HIG + msg + NOR)
#define YELLOW_MSG(msg)         (HIY + msg + NOR)
#define BLUE_MSG(msg)           (HIB + msg + NOR)
#define CYAN_MSG(msg)           (HIC + msg + NOR)
#define WHITE_MSG(msg)          (HIW + msg + NOR)

#endif // __GAME_ANSI_H__
