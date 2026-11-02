/**
 * @file    living.c
 * @brief   活体实体基类



 *
 * @par Description:
 * 活体实体基类，统一整合动作、战斗、命令、心跳与生物属性能力。
 */

#pragma strict_types

#include <system/globals.h>
#include <system/constants.h>
#include <system/daemon.h>
#include <system/ansi.h>
#include <system/services.h>

inherit "/lib/base/object";

inherit "/kernel/framework/action";
inherit "/kernel/framework/attack";
inherit "/kernel/framework/damage";

inherit "/kernel/framework/command";

inherit "/kernel/framework/heartbeat/heartbeat_batch";
inherit "/kernel/framework/heartbeat/heartbeat_optimizer";

inherit "/kernel/framework/skill";
inherit "/lib/base/vitals";
inherit "/lib/base/encumbrance";
inherit "/lib/base/advancement";
inherit "/lib/base/living_attributes";
inherit "/lib/base/living_action_state";
inherit "/daemons/modules/buff/buff_module";

void on_living_visibility_changed(object room)
{
    if (objectp(room))
        ROOM_VIEW_D->invalidate_room_view_content(room);
}
