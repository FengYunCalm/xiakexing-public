/**
 * @file    cleanup.c
 * @brief   清理框架兼容入口
 * @author  XiaKeXing Development Team
 * @date    2026-03-20
 * @version 1.0.0
 *
 * @par Description:
 * 清理框架兼容入口文件。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>

nosave int _no_cleanup;
nosave int _last_active_time;

void create()
{
    _no_cleanup = 0;
    _last_active_time = time();
}

/**
 * @brief 标记对象最近活跃时间
 * @note 调用方应在真实交互、状态写入或资源占用发生时更新，避免对象被误清理。
 */
void mark_active()
{
    _last_active_time = time();
}

int query_last_active_time()
{
    return _last_active_time;
}

void set_no_cleanup(int flag)
{
    _no_cleanup = flag ? 1 : 0;
}

int query_no_cleanup()
{
    return _no_cleanup;
}

/**
 * @brief 驱动清理回调
 * @param inherited 驱动传入的继承层标记（当前实现不使用）
 * @return 1=保留对象并保留复检（下次仍会调用）；0=保留对象且不再复查（或已自行销毁）
 * @note 交互对象、仍在环境中的对象、仍有内容物的对象必须保留并保持复检；只有
 *       无内容且未禁清理的空对象才允许析构。容器与房间共用本回调，保护点必须
 *       在真实析构处，不能只在调用方拦截。
 */
int clean_up(int inherited)
{
    if (_no_cleanup)
        return 0;

    if (interactive(this_object()))
        return 1;

    if (environment(this_object()))
        return 1;

    /* 仍有玩家/物品/货物的房间与容器不得销毁：清理回调不参与内容物迁移，
     * 一旦析构就会连同使用中对象一起丢失。返回非零使对象留在清理队列中
     * 继续复查，内容物清空后仍可被回收；返回 0 会永久移出清理队列。 */
    if (sizeof(all_inventory(this_object())) > 0)
        return 1;

    destruct(this_object());
    return 0;
}
