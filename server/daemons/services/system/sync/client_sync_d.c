/**
 * @file    client_sync_d.c
 * @brief   客户端状态同步协调服务



 *
 * @par Description:
 * 统一负责服务端到客户端的状态同步编排。
 * 业务服务只声明本次动作影响了哪些客户端视图，具体 XK 状态、背包、房间快照
 * 的发送顺序与去重由本门面收口；内部按计划构造、即时包发送和 flush 编排拆分。
 */

#pragma strict_types
#pragma save_types

inherit "/lib/framework/daemon";
inherit "/daemons/services/system/sync/client_sync_plan";
inherit "/daemons/services/system/sync/client_sync_packets";
inherit "/daemons/services/system/sync/client_sync_flush";

void setup_client_sync_flush();
mapping tx_plan_full_player_snapshot(mapping tx, object room);

int health_check()
{
    return function_exists("query_status", this_object()) ? 1 : 0;
}

void setup()
{
    setup_client_sync_flush();
}

void request_full_player_snapshot(object player)
{
    if (!objectp(player))
        return;
    submit_transaction(player,
        tx_plan_full_player_snapshot(0, environment(player)));
}
