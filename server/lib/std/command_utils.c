/**
 * @file    command_utils.c
 * @brief   命令工具库 - compatibility helper facade



 *
 * @par Description:
 * compatibility helper facade，命令层统一从这里继承常用参数解析、查找与消息工具。
 * 真正 helper 实现已拆到 `util_*` 子模块；本文件负责兼容旧入口与聚合复用，不承载业务真逻辑。
 *
 * 已拆分为以下模块：
 * - /lib/std/utils/util_args.c    - 参数解析
 * - /lib/std/utils/util_input.c   - 输入验证
 * - /lib/std/utils/util_item.c    - 物品查找
 * - /lib/std/utils/util_player.c  - 玩家查找
 * - /lib/std/utils/util_status.c  - 状态检查
 * - /lib/std/utils/util_message.c - 消息发送
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/daemon.h>
#include <system/ansi.h>
#include <system/constants.h>
#include <game_constants.h>

inherit "/lib/std/utils/util_args";
inherit "/lib/std/utils/util_direction";
inherit "/lib/std/utils/util_item";
inherit "/lib/std/utils/util_player";
inherit "/lib/std/utils/util_status";
inherit "/lib/std/utils/util_message";

object load_shop_service();

object find_vendor_in_room(object me)
{
    object shop_d;

    if (!objectp(me))
        return 0;

    shop_d = load_shop_service();
    if (!objectp(shop_d))
        return 0;

    return shop_d->find_shop_npc(me);
}

object load_shop_service()
{
    object shop_d;

    shop_d = find_object(SHOP_D);
    if (!objectp(shop_d))
        shop_d = load_object(SHOP_D);

    return shop_d;
}
