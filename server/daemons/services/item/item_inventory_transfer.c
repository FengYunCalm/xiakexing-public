/**
 * @file    item_inventory_transfer.c
 * @brief   物品给予、玩家间转移和附件转换模块。
 */

#pragma strict_types
#pragma save_types

#include <system/ansi.h>
#include <system/services.h>
#include <framework/message_facade.h>

string query_item_display_name(object item);
string query_actor_display_name(object actor, string fallback);
object find_item_by_name(object me, string item_name);
int is_equipped_item(object item);
int can_receive_target(object target);
int query_item_stack_amount(object item);
void set_item_stack_amount(object item, int amount);
object create_item_stack_slice(object item, int count);
void send_xk_quick_cmds(object me, string commands, int append_inventory,
    mapping response_context);

private int rollback_give_transfer(object me, object item, object transfer_item,
    int original_stack_amount)
{
    if (!objectp(me) || !objectp(item))
        return 0;
    if (transfer_item == item) {
        if (!transfer_item->move(me))
            return 0;
        ITEM_D->bind_item_owner(transfer_item, me, "give_rollback");
        return 1;
    }
    if (objectp(transfer_item))
        destruct(transfer_item);
    if (environment(item) != me)
        return 0;
    set_item_stack_amount(item, original_stack_amount);
    return 1;
}

varargs int give_item(object me, object target, string item_name, int count,
    mapping response_context)
{
    object item;
    object transfer_item;
    mapping favor_result;
    int rollback_ok;
    int stack_amount;
    string target_name;

    response_context = mapp(response_context) ? copy(response_context) : 0;
    if (!objectp(me) || !objectp(target) || !stringp(item_name) || item_name == "")
        return 0;
    if (count <= 0)
        count = 1;

    item = find_item_by_name(me, item_name);
    if (!objectp(item)) {
        msg_error(me, "你身上没有「" + item_name + "」。\n", "give", "item_not_found", item_name, 0,
            response_context);
        return 1;
    }

    target_name = query_actor_display_name(target, "对方");
    if (!can_receive_target(target)) {
        msg_error(me, HIY + target_name + "无法接收物品。" NOR + "\n",
            "give", "target_cannot_receive", item_name, 0, response_context);
        return 1;
    }
    if (target == me) {
        msg_error(me, HIY "给自己？" NOR + "\n", "give", "self_target", item_name, 0,
            response_context);
        return 1;
    }
    if (is_equipped_item(item)) {
        msg_error(me, HIR + query_item_display_name(item) + "已装备，请先卸下。" NOR + "\n",
            "give", "item_equipped", item_name, 0, response_context);
        return 1;
    }
    if (item->query("no_give")) {
        msg_error(me, HIR + query_item_display_name(item) + "是绑定物品，无法给予他人。" NOR + "\n",
            "give", "no_give", item_name, 0, response_context);
        return 1;
    }
    stack_amount = query_item_stack_amount(item);
    if (count > stack_amount) {
        msg_error(me, sprintf("你身上只有%d个%s。\n", stack_amount, query_item_display_name(item)),
            "give", "insufficient_stack", item_name, 0, response_context);
        return 1;
    }
    if (function_exists("is_npc", target) && call_other(target, "is_npc")) {
        if (!function_exists("can_accept_give_item", target)
            || !(target->can_accept_give_item(me, item, count))) {
            if (!function_exists("reject_give_item", target)
                || !(target->reject_give_item(me, item, count)))
                msg_error(me, target_name + "摆了摆手：这东西你自己留着吧，我用不上。\n",
                    "give", "npc_reject", item_name, 0, response_context);
            return 1;
        }
    }
    transfer_item = item;
    if (item->query("stackable") && count < stack_amount) {
        transfer_item = create_item_stack_slice(item, count);
        if (!objectp(transfer_item)) {
            msg_error(me, "这件物品暂时无法拆分给予。\n", "give", "split_failed", item_name, 0,
                response_context);
            return 1;
        }
    }
    if (interactive(target)
        && !call_other(ITEM_INVENTORY_D, "can_add_item_kind", target, transfer_item)) {
        if (transfer_item != item)
            destruct(transfer_item);
        msg_error(me, target_name + "的背包已满，装不下更多种类的物品。\n",
            "give", "target_kind_capacity_full", item_name, 0, response_context);
        msg_error(target, "你的背包已满，装不下更多种类的物品。\n",
            "give", "inventory_kind_capacity_full", item_name, 0);
        return 1;
    }
    if (!transfer_item->move(target)) {
        if (transfer_item != item)
            destruct(transfer_item);
        msg_error(me, HIY + target_name + "拿不了更多东西了。" NOR + "\n",
            "give", "target_inventory_full", item_name, 0, response_context);
        msg_error(target, HIY + query_actor_display_name(me, "有人") + "想给你" + query_item_display_name(item) + "，但你拿不了。" NOR + "\n",
            "give", "inventory_full", item_name, 0);
        return 1;
    }
    ITEM_D->bind_item_owner(transfer_item, target, "give");
    if (transfer_item != item)
        set_item_stack_amount(item, stack_amount - count);

    /* 好感和物品给予必须保持可恢复：好感持久化失败时先撤销物品
     * 转移，不向玩家发出“已送出”的成功反馈。 */
    if (function_exists("is_npc", target) && call_other(target, "is_npc")) {
        favor_result = REPUTATION_D->apply_npc_favor_give(me, target,
            transfer_item, count);
        if (!mapp(favor_result) || !favor_result["ok"]) {
            rollback_ok = rollback_give_transfer(me, item, transfer_item,
                stack_amount);
            msg_error(me, rollback_ok
                ? "好感记录暂未保存，物品已退回，请稍后重试。\n"
                : "好感记录暂未保存，物品转移待恢复，请联系管理员。\n",
                "give", rollback_ok ? "favor_persist_failed"
                    : "favor_compensation_required", item_name, 0, response_context);
            return 1;
        }
    }

    msg_system(me, HIG + "你把" + query_item_display_name(transfer_item) + "给了" + target_name + "。" NOR + "\n",
        "item_give", response_context);
    msg_system(target, HIG + query_actor_display_name(me, "有人") + "给了你" + query_item_display_name(transfer_item) + "。" NOR + "\n",
        "item_receive");
    msg_to_room(environment(me), query_actor_display_name(me, "有人") + "给了" + target_name + "一样东西。\n", ({ me, target }));

    QUEST_D->record_give_progress(me, target, transfer_item, count);
    ITEM_FEEDBACK_D->sync_transfer(me, target, response_context);
    /* 任务进度反馈由 record_give_progress 的 mutation 统一投递进度块，
     * 此处不再叠加 provider 全量摘要（"【你当前相关的任务】"刷主文本区事故）。 */
    if (function_exists("schedule_save", me))
        me->schedule_save();
    if (userp(target) && function_exists("schedule_save", target))
        target->schedule_save();
    send_xk_quick_cmds(me, "操作|查看物品:i|查看属性:score", 1,
        response_context);
    return 1;
}

object query_transferable_item(object me, string item_name)
{
    object item;

    if (!objectp(me) || !stringp(item_name) || item_name == "")
        return 0;

    item = find_item_by_name(me, item_name);
    if (!objectp(item))
        return 0;
    if (is_equipped_item(item))
        return 0;
    if (item->query("no_give") || item->query("no_drop"))
        return 0;

    return item;
}

mapping build_item_attachment_from_object(object item)
{
    mixed ids;
    string lookup_id;
    string template_id;
    string item_path;
    int amount;

    if (!objectp(item))
        return 0;
    if (item->query("no_give") || item->query("no_drop"))
        return 0;
    if (is_equipped_item(item))
        return 0;

    template_id = item->query("template_id") || "";
    ids = function_exists("query_id", item)
        ? item->query_id() : get_object_id(item);
    if (template_id == "") {
        if (stringp(ids))
            template_id = ids;
        else if (arrayp(ids) && sizeof(ids) > 0)
            template_id = ids[0];
    }

    lookup_id = stringp(template_id) && template_id != "" ? template_id : "";
    if (lookup_id == "" && stringp(ids))
        lookup_id = ids;
    else if (lookup_id == "" && arrayp(ids) && sizeof(ids) > 0)
        lookup_id = ids[0];

    if (template_id != "" && !ITEM_D->item_exists(template_id))
        template_id = "";

    item_path = base_name(item);
    if (template_id == "" && (!stringp(item_path) || item_path == ""))
        return 0;

    amount = item->query("amount");
    if (!intp(amount) || amount <= 0)
        amount = item->query("count");
    if (!intp(amount) || amount <= 0)
        amount = 1;
    if (!item->query("stackable"))
        amount = 1;

    return ([
        "template_id": template_id,
        "lookup_id": lookup_id,
        "item_path": item_path,
        "count": amount,
        "item_name": query_item_display_name(item),
    ]);
}

int consume_attachment_item_from_player(object me, mapping attachment)
{
    object item;
    int amount;
    int remain;
    string item_name;
    string lookup_id;
    string template_id;

    if (!objectp(me) || !mapp(attachment))
        return 0;

    item_name = attachment["item_name"] || "";
    lookup_id = attachment["lookup_id"] || "";
    template_id = attachment["template_id"] || "";
    item = objectp(attachment["item"]) ? attachment["item"] : 0;
    if (!objectp(item) || environment(item) != me) {
        if (item_name != "")
            item = present(item_name, me);
        if (!objectp(item) && lookup_id != "")
            item = present(lookup_id, me);
        if (!objectp(item) && template_id != "")
            item = present(template_id, me);
    }
    if (!objectp(item) || environment(item) != me)
        return 0;
    if (is_equipped_item(item))
        return 0;

    amount = item->query("amount");
    if (!intp(amount) || amount <= 0)
        amount = item->query("count");
    if (!intp(amount) || amount <= 0)
        amount = 1;

    if (item->query("stackable")) {
        remain = amount - (attachment["count"] || 1);
        if (remain < 0)
            return 0;
        if (remain == 0) {
            destruct(item);
            return 1;
        }
        item->set("amount", remain);
        item->set("count", remain);
        return 1;
    }

    destruct(item);
    return 1;
}

/* L-06 配套：附件被消耗后投递失败时必须能退回，不能凭空销毁玩家资产。
 * 退回走物品 owner：按 template_id 重建物品并放入玩家背包，容量不足返回 0。 */
int restore_attachment_to_player(object me, mapping attachment)
{
    object item;
    int count;

    if (!objectp(me) || !mapp(attachment))
        return 0;
    count = intp(attachment["count"]) && attachment["count"] > 0
        ? attachment["count"] : 1;
    if (!stringp(attachment["template_id"]) || attachment["template_id"] == "")
        return 0;
    item = ITEM_D->create_item(attachment["template_id"], count);
    if (!objectp(item))
        return 0;
    if (!item->move(me)) {
        destruct(item);
        return 0;
    }
    ITEM_D->bind_item_owner(item, me, "attachment_restore");
    if (function_exists("query_xk_inventory_generation", me))
        ITEM_FEEDBACK_D->invalidate_inventory_projection(me);
    return 1;
}

private int item_has_transfer_operation(object item, string operation_id,
    object target)
{
    mapping operations;
    string target_id;

    if (!objectp(item) || !stringp(operation_id) || operation_id == ""
        || !objectp(target))
        return 0;
    operations = item->query("economy/trade_transfer_operations");
    target_id = get_object_id(target);
    return mapp(operations) && stringp(target_id) && target_id != ""
        && operations[operation_id] == target_id;
}

private void mark_transfer_operation(object item, string operation_id,
    object target)
{
    mapping operations;
    string target_id;

    if (!objectp(item) || !stringp(operation_id) || operation_id == ""
        || !objectp(target))
        return;
    target_id = get_object_id(target);
    if (!stringp(target_id) || target_id == "")
        return;
    operations = item->query("economy/trade_transfer_operations");
    operations = mapp(operations) ? copy(operations) : ([]);
    operations[operation_id] = target_id;
    item->set("economy/trade_transfer_operations", operations);
}

private void clear_transfer_operation(object item, string operation_id)
{
    mapping operations;

    if (!objectp(item) || !stringp(operation_id) || operation_id == "")
        return;
    operations = item->query("economy/trade_transfer_operations");
    if (!mapp(operations) || undefinedp(operations[operation_id]))
        return;
    operations = copy(operations);
    map_delete(operations, operation_id);
    item->set("economy/trade_transfer_operations", operations);
}

private int transfer_operation_conflict(object item, string operation_id,
    object target)
{
    mapping operations;
    string target_id;

    if (!objectp(item) || !stringp(operation_id) || operation_id == ""
        || !objectp(target))
        return 0;
    operations = item->query("economy/trade_transfer_operations");
    target_id = get_object_id(target);
    return mapp(operations) && !undefinedp(operations[operation_id])
        && operations[operation_id] != target_id;
}

int query_item_transfer_operation(object item, string operation_id,
    object target)
{
    return item_has_transfer_operation(item, operation_id, target);
}

mapping transfer_item_between_players_with_operation(object from, object to,
    object item, string operation_id)
{
    if (!objectp(from) || !objectp(to) || !objectp(item))
        return ([ "ok": 0, "error": "invalid_arguments" ]);
    if (stringp(operation_id) && operation_id != ""
        && transfer_operation_conflict(item, operation_id, to))
        return ([ "ok": 0, "error": "operation_conflict" ]);
    if (stringp(operation_id) && operation_id != ""
        && item_has_transfer_operation(item, operation_id, to)
        && environment(item) == to)
        return ([
            "ok": 1,
            "duplicate": 1,
            "item": item,
            "from": from,
            "to": to,
            "operation_id": operation_id,
        ]);
    if (environment(item) != from)
        return ([ "ok": 0, "error": "item_not_owned" ]);
    if (!can_receive_target(to))
        return ([ "ok": 0, "error": "invalid_target" ]);
    if (is_equipped_item(item))
        return ([ "ok": 0, "error": "item_equipped" ]);
    if (item->query("no_give") || item->query("no_drop"))
        return ([ "ok": 0, "error": "item_locked" ]);
    if (!call_other(ITEM_INVENTORY_D, "can_add_item_kind", to, item))
        return ([ "ok": 0, "error": "target_kind_capacity_full" ]);
    if (stringp(operation_id) && operation_id != "")
        mark_transfer_operation(item, operation_id, to);
    if (!item->move(to)) {
        if (stringp(operation_id) && operation_id != "")
            clear_transfer_operation(item, operation_id);
        return ([ "ok": 0, "error": "target_cannot_hold" ]);
    }
    ITEM_D->bind_item_owner(item, to, "player_transfer");
    if (stringp(operation_id) && operation_id != "") {
        if (function_exists("query_xk_inventory_generation", from))
            ITEM_FEEDBACK_D->invalidate_inventory_projection(from);
        if (function_exists("query_xk_inventory_generation", to))
            ITEM_FEEDBACK_D->invalidate_inventory_projection(to);
    }

    return ([
        "ok": 1,
        "item": item,
        "from": from,
        "to": to,
        "operation_id": operation_id,
    ]);
}

mapping transfer_item_between_players(object from, object to, object item)
{
    mapping result;

    result = transfer_item_between_players_with_operation(from, to, item, "");
    if (mapp(result) && result["ok"] == 1) {
        if (function_exists("query_xk_inventory_generation", from))
            ITEM_FEEDBACK_D->invalidate_inventory_projection(from);
        if (function_exists("query_xk_inventory_generation", to))
            ITEM_FEEDBACK_D->invalidate_inventory_projection(to);
    }
    return result;
}

int rollback_player_item_transfer(mapping transfer)
{
    object from;
    object item;
    object to;

    if (!mapp(transfer) || !transfer["ok"])
        return 0;

    item = transfer["item"];
    from = transfer["from"];
    to = transfer["to"];
    if (!objectp(item) || !objectp(from))
        return 0;

    if (!item->move(from))
        return 0;
    ITEM_D->bind_item_owner(item, from, "player_transfer_rollback");
    if (function_exists("query_xk_inventory_generation", from))
        ITEM_FEEDBACK_D->invalidate_inventory_projection(from);
    if (objectp(to) && function_exists("query_xk_inventory_generation", to))
        ITEM_FEEDBACK_D->invalidate_inventory_projection(to);
    clear_transfer_operation(item, transfer["operation_id"] || "");
    return 1;
}
