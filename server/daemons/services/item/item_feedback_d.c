/**
 * @file    item_feedback_d.c
 * @brief   物品状态反馈/同步辅助服务（客户端物品视图投影的反馈补发与同步桥）。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

/* Mutation producers invalidate here; inventory delivery remains read-only. */
void invalidate_inventory_projection(object player)
{
    if (!objectp(player))
        return;

    call_other("/kernel/simul_efun", "invalidate_xk_inventory_cache", player);
}

mapping build_item_client_payload(object item, mapping result)
{
    return CLIENT_PROJECTION_D->build_item_client_payload(item, result);
}

mapping build_item_client_payload_from_template_id(string item_id, object item)
{
    return CLIENT_PROJECTION_D->build_item_client_payload_from_template_id(item_id, item);
}

varargs void sync_inventory(object player, mapping response_context)
{
    if (!objectp(player))
        return;

    invalidate_inventory_projection(player);
    CLIENT_SYNC_D->request_inventory(player, response_context);
}

varargs void sync_status(object player, mapping response_context)
{
    if (!objectp(player))
        return;

    CLIENT_SYNC_D->request_status(player, response_context);
}

varargs void sync_inventory_and_status(object player, mapping response_context)
{
    if (!objectp(player))
        return;

    invalidate_inventory_projection(player);
    CLIENT_SYNC_D->request_status_inventory(player, response_context);
}

varargs void sync_room_snapshot(object player, object room,
    mapping response_context)
{
    if (!objectp(player) || !objectp(room))
        return;

    CLIENT_SYNC_D->request_room_snapshot(player, room, response_context);
}

varargs void sync_consumable_use(object player, object source_env,
    mapping response_context)
{
    if (!objectp(player))
        return;
    if (source_env == player) {
        invalidate_inventory_projection(player);
        CLIENT_SYNC_D->request_status_inventory(player, response_context);
        return;
    }

    if (objectp(source_env)) {
        CLIENT_SYNC_D->request_status_room_snapshot(player, source_env,
            response_context);
        return;
    }

    CLIENT_SYNC_D->request_status(player, response_context);
}

varargs void sync_equipment_change(object player, mapping response_context)
{
    if (objectp(player)
        && function_exists("invalidate_xk_score_projection", player))
        player->invalidate_xk_score_projection(1);
    // 装备词条入派生属性与 vital maxima 输入面：变更即失效（B1 读模型键源；
    // maxima 变更同步 hp 对话位）
    if (objectp(player)
        && function_exists("invalidate_xk_vital_maxima_projection", player))
        player->invalidate_xk_vital_maxima_projection(1);
    if (!objectp(player))
        return;

    invalidate_inventory_projection(player);
    CLIENT_SYNC_D->request_status_inventory_score_refresh(player,
        response_context);
}

varargs void sync_inventory_room_change(object player, object room,
    mapping response_context)
{
    if (!objectp(player))
        return;

    invalidate_inventory_projection(player);
    if (objectp(room)) {
        CLIENT_SYNC_D->request_inventory_room(player, room, response_context);
        return;
    }

    CLIENT_SYNC_D->request_inventory(player, response_context);
}

varargs void sync_transfer(object actor, object target,
    mapping response_context)
{
    if (objectp(actor))
        sync_inventory(actor, response_context);

    if (objectp(target) && interactive(target))
        sync_inventory(target, 0);
}

varargs void sync_repair_result(object player, object item, mapping result,
    mapping response_context)
{
    mapping extra_payload;
    mapping tx;
    string item_name;

    if (!objectp(player) || !mapp(result))
        return;

    response_context = mapp(response_context) ? copy(response_context) : 0;
    item_name = objectp(item) ? (item->query_name() || item->query("name") || "") : "";
    extra_payload = build_item_client_payload(item, result);
    tx = CLIENT_SYNC_D->tx_add_trade_result(0, "repair", item_name,
        result["cost"] || 0, result["ok"], extra_payload);
    tx = CLIENT_SYNC_D->tx_plan_inventory(tx);
    invalidate_inventory_projection(player);
    tx = CLIENT_SYNC_D->tx_with_response_context(tx, response_context);
    CLIENT_SYNC_D->submit_transaction(player, tx);
}

varargs void sync_forge_result(object player, mapping result,
    mapping response_context)
{
    string action;
    mapping extra_payload;
    mapping tx;
    object item;
    string item_name;

    if (!objectp(player) || !mapp(result))
        return;

    response_context = mapp(response_context) ? copy(response_context) : 0;
    action = result["action"] || "forge";
    item = objectp(result["item"]) ? result["item"] : 0;
    item_name = objectp(item) ? (item->query_name() || item->query("name") || "") : "";
    extra_payload = build_item_client_payload(item, result);
    tx = CLIENT_SYNC_D->tx_add_trade_result(0, action, item_name,
        result["cost"] || 0, result["ok"], extra_payload);
    tx = CLIENT_SYNC_D->tx_plan_inventory(tx);
    invalidate_inventory_projection(player);
    tx = CLIENT_SYNC_D->tx_with_response_context(tx, response_context);
    CLIENT_SYNC_D->submit_transaction(player, tx);
}
