/**
 * @file    diag.c
 * @brief   诊断命令 - 查看角色与战斗状态



 *
 * @par Description:
 * 输出当前角色的房间、协议与战斗快照，供运行期诊断使用。
 * 本命令只读展示状态，不承担任何修复或维护动作。
 */

#pragma strict_types

#include <system/ansi.h>
#include <system/services.h>

inherit "/lib/framework/command";
inherit "/lib/std/utils/util_player";
inherit "/lib/std/utils/util_status";

int help(object me);

int main(object me, string arg)
{
    object env;
    string combat_id;
    string protocol_state;
    string room_name;
    mapping combat;
    mapping read_summary;
    mapping resources;
    mapping stats;
    string opponent;

    if (!objectp(me))
        return 0;

    env = environment(me);
    combat_id = COMBAT_D->query_combat_id(me);
    combat = stringp(combat_id) ? COMBAT_D->query_combat(combat_id) : 0;
    read_summary = READ_SUMMARY_D->query_diag_summary(me);
    resources = query_combat_resource_snapshot(me);
    stats = COMBAT_D->query_combat_stats();
    opponent = "无";

    if (mapp(combat)) {
        if (objectp(combat["attacker"]) && combat["attacker"] != me)
            opponent = get_object_display_name(combat["attacker"], "对手");
        else if (objectp(combat["defender"]) && combat["defender"] != me)
            opponent = get_object_display_name(combat["defender"], "对手");
    }

    protocol_state = me->query_temp("xk_protocol_version");
    if (!stringp(protocol_state) || protocol_state == "") {
        if (interactive(me))
            protocol_state = "未协商（兼容协议发送）";
        else
            protocol_state = "未协商";
    }

    msg_send(me, HIC "[诊断] 角色/房间/战斗\n" NOR);
    msg_send(me, "账号: " + get_object_display_id(me, "无") + "\n");
    msg_send(me, "姓名: " + get_object_display_name(me, "无") + "\n");
    room_name = objectp(env) ? get_object_short_name(env, base_name(env)) : "无";

    msg_send(me, "房间: " + room_name + "\n");
    msg_send(me, "XK协议: " + protocol_state + "\n");
    msg_send(me, "气血: " + resources["hp"] + "/" + resources["max_hp"] + "\n");
    msg_send(me, "内力: " + resources["mp"] + "/" + resources["max_mp"] + "\n");
    msg_send(me, "战斗中: " + (COMBAT_D->is_in_combat(me) ? "是" : "否") + "\n");
    msg_send(me, "战斗ID: " + (combat_id || "无") + "\n");
    msg_send(me, "对手: " + opponent + "\n");
    msg_send(me, "回合: " + (mapp(combat) ? combat["round"] : 0) + "\n");
    msg_send(me, "模式: " + (mapp(combat) && combat["kill_mode"] ? "生死斗" : "切磋/无") + "\n");
    msg_send(me, "全局战斗数: " + (stats["total_combats"] || 0) + "\n");
    msg_send(me, "读侧worker: " + (mapp(read_summary) ? (read_summary["async_state"] || "pending") : "pending") + "\n");
    return 1;
}

int help(object me)
{
    msg_send(me, @HELP
命令：diag（诊断）

查看当前角色的登录、房间、协议与战斗诊断状态。

用法：
  diag            查看当前诊断信息

显示内容：
  - 当前账号、姓名、房间
  - XK 协议协商状态
  - 当前气血/内力快照
  - 当前战斗 ID、对手、回合、模式

相关命令：
  score   - 查看角色状态
  status  - 查看服务器概览

HELP
    );
    return 1;
}
