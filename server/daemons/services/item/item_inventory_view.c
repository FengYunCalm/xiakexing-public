/**
 * @file    item_inventory_view.c
 * @brief   背包展示、拾取和丢弃模块。
 */

#pragma strict_types
#pragma save_types

#include <system/ansi.h>
#include <system/services.h>
#include <framework/message_facade.h>

int item_inventory_supports_xk_protocol(object target);
string query_item_display_name(object item);
string query_actor_display_name(object actor, string fallback);
object resolve_item_from_collection(object *items, string item_name);
object find_item_by_name(object me, string item_name);
int is_equipped_item(object item);
void send_xk_inventory(object me, object *obs);

string format_equipped_items(object *equip_list)
{
    string result;
    object item;

    if (!arrayp(equip_list) || sizeof(equip_list) == 0)
        return "";

    result = HIY "【已装备】" NOR "\n";
    foreach (item in equip_list)
        result += sprintf("  %s %s\n", HIG "■" NOR, query_item_display_name(item));

    return result + "\n";
}

string format_item_name(object item)
{
    string name;
    int amount;

    name = query_item_display_name(item);
    amount = function_exists("query_amount", item) ? item->query_amount() : 0;
    if (item->query("stackable") && amount > 0)
        name = sprintf("%d%s%s", amount, item->query("base_unit") || "个", name);
    return name;
}

private int item_is_unique_per_player(object item)
{
    return objectp(item) && item->query("unique_per_player");
}

private int item_claimed_by_other_player(object player, object item)
{
    string binding_policy;
    string owner_id;
    string player_id;

    if (!objectp(player) || !objectp(item))
        return 0;
    binding_policy = item->query("binding_policy")
        || item->query("acquisition/binding_policy") || "none";
    if (binding_policy == "none")
        return 0;
    owner_id = item->query("owner_id") || "";
    if (owner_id == "")
        return 0;
    player_id = ITEM_D->query_binding_owner_id(player, binding_policy);
    return owner_id != player_id;
}

private int is_room_scoped_target(string arg)
{
    return stringp(arg) && strlen(arg) > 5 && arg[0..4] == "room:";
}

private string strip_room_scope(string arg)
{
    return is_room_scoped_target(arg) ? arg[5..] : arg;
}

private void rollback_world_event_placement(object owner, object room,
    mapping placement)
{
    object placed;
    int count;
    int amount;

    if (!objectp(owner) || !objectp(room) || !mapp(placement)
        || !objectp(placement["item"]))
        return;
    placed = placement["item"];
    count = intp(placement["count"]) && placement["count"] > 0
        ? placement["count"] : 1;
    if (placement["merged"]) {
        amount = ITEM_INVENTORY_D->query_item_stack_amount(placed);
        ITEM_INVENTORY_D->set_item_stack_amount(placed, amount - count);
        return;
    }
    if (environment(placed) == owner && placed->move(room)) {
        placed->set("owner_id", "");
        placed->set("ownership/provenance", "");
    }
}

private string query_collect_progress_item_id(object item)
{
    mixed ids;

    if (!objectp(item))
        return "";
    if (stringp(item->query("template_id")) && item->query("template_id") != "")
        return item->query("template_id");

    ids = function_exists("query_id", item)
        ? item->query_id() : get_object_id(item);
    if (stringp(ids) && ids != "")
        return ids;
    if (arrayp(ids) && sizeof(ids) > 0 && stringp(ids[0]) && ids[0] != "")
        return ids[0];
    return "";
}

private string query_unique_inventory_key(object item)
{
    mixed ids;
    string template_id;
    string path;

    if (!item_is_unique_per_player(item))
        return "";

    template_id = item->query("template_id") || "";
    if (template_id != "")
        return "template:" + template_id;

    ids = function_exists("query_id", item)
        ? item->query_id() : get_object_id(item);
    if (stringp(ids) && ids != "")
        return "id:" + ids;
    if (arrayp(ids) && sizeof(ids) > 0 && stringp(ids[0]) && ids[0] != "")
        return "id:" + ids[0];

    path = base_name(item);
    return stringp(path) && path != "" ? "path:" + path : "";
}

void normalize_unique_inventory_items(object me)
{
    mapping seen;
    object item;
    string key;

    if (!objectp(me))
        return;

    seen = ([]);
    foreach (item in all_inventory(me)) {
        key = query_unique_inventory_key(item);
        if (key == "")
            continue;
        if (objectp(seen[key])) {
            destruct(item);
            continue;
        }
        seen[key] = item;
    }
}

private object find_matching_unique_inventory_item(object me, object source)
{
    object item;
    string source_key;

    if (!objectp(me) || !objectp(source))
        return 0;

    source_key = query_unique_inventory_key(source);
    if (source_key == "")
        return 0;

    foreach (item in all_inventory(me)) {
        if (item != source && query_unique_inventory_key(item) == source_key)
            return item;
    }

    return 0;
}

string format_unequipped_items(object *inv)
{
    string result;
    string status;
    object item;

    result = "";
    foreach (item in inv) {
        if (is_equipped_item(item))
            continue;

        status = "";
        if (item->query("no_drop") || item->query("no_give"))
            status = HIW " √" NOR;
        result += sprintf("  %s%s\n", format_item_name(item), status);
    }

    return result;
}

varargs int show_inventory(object me, mapping response_context)
{
    object *inv;
    object *equip_list;
    string result;
    int use_xk;

    if (!objectp(me))
        return 0;

    use_xk = item_inventory_supports_xk_protocol(me);
    if (use_xk) {
        /*
         * Explicit readonly opens can reuse the cached IVTY payload directly.
         * Mutation-driven refreshes still invalidate through ITEM_FEEDBACK_D
         * before going through CLIENT_SYNC_D.
         */
        call_other("/kernel/simul_efun", "xk_send_inventory", me, 0,
            response_context);
        return 1;
    }

    normalize_unique_inventory_items(me);
    inv = all_inventory(me);
    if (sizeof(inv) == 0) {
        msg_send(me, "你身上没有携带任何东西。\n", response_context);
        return 1;
    }

    equip_list = filter_array(inv, (: is_equipped_item($1) :));
    result = HIC "═══════════════ 物品栏 ═══════════════" NOR "\n";
    result += format_equipped_items(equip_list);
    result += format_unequipped_items(inv);
    msg_send(me, result, response_context);
    return 1;
}

varargs int pickup_item(object me, string item_name, mapping response_context)
{
    mapping placement;
    object env;
    object item;
    string actor_name;
    string display_name;
    string item_id;
    string world_event_id;
    string respawn_entity_id;
    string lookup_name;
    int release_ok;
    int world_event_reserved;
    int room_scoped;

    response_context = mapp(response_context) ? copy(response_context) : 0;
    if (!objectp(me) || !stringp(item_name) || item_name == "")
        return 0;

    env = environment(me);
    if (!objectp(env)) {
        msg_error(me, "你现在拿不了任何东西。\n", "get", "missing_environment", item_name, 0,
            response_context);
        return 1;
    }

    room_scoped = is_room_scoped_target(item_name);
    lookup_name = strip_room_scope(item_name);
    item = present(lookup_name, env);
    if (!objectp(item))
        item = resolve_item_from_collection(all_inventory(env), lookup_name);
    if (!objectp(item)) {
        msg_error(me, "这里没有这样东西。\n", "get", "item_not_found", lookup_name, 0,
            response_context);
        return 1;
    }

    if (living(item)) {
        msg_error(me, "那是活物，不能这样捡。\n", "get", "target_is_living", item_name, 0,
            response_context);
        return 1;
    }

    if (item->query("no_get")) {
        msg_error(me, "这样东西拿不起来。\n", "get", "no_get", item_name, 0,
            response_context);
        return 1;
    }

    ITEM_D->ensure_item_identity(item);
    if (item_claimed_by_other_player(me, item)) {
        msg_error(me, "这件战利品已有归属。\n", "get", "loot_claimed", item_name, 0,
            response_context);
        return 1;
    }

    normalize_unique_inventory_items(me);
    if (objectp(find_matching_unique_inventory_item(me, item))) {
        msg_error(me, "你已经带着" + query_item_display_name(item) + "。\n", "get", "unique_item_exists", room_scoped ? item_name : lookup_name, 0,
            response_context);
        ITEM_FEEDBACK_D->sync_inventory_room_change(me, env, response_context);
        return 1;
    }

    // 入包合并可能销毁地面对象；名称和任务标识必须在移交前取得。
    display_name = query_item_display_name(item);
    item_id = query_collect_progress_item_id(item);
    world_event_id = stringp(item->query("world_event_id"))
        ? item->query("world_event_id") : "";
    respawn_entity_id = WORLD_ENTITY_LIFECYCLE_D->query_pickup_respawn_entity(env, item);
    if (world_event_id != "") {
        world_event_reserved = WORLD_EVENT_D->reserve_item_pickup(
            world_event_id, item, env);
        if (!world_event_reserved) {
            msg_error(me, "这件奇遇物品的状态暂未保存，请稍后再试。\n",
                "get", "world_event_persist_failed", item_name, 0, response_context);
            ITEM_FEEDBACK_D->sync_inventory_room_change(me, env, response_context);
            return 1;
        }
    }
    placement = call_other(ITEM_INVENTORY_D, "add_item_to_inventory", me, item);
    if (!mapp(placement) || !placement["ok"]) {
        if (world_event_reserved) {
            release_ok = WORLD_EVENT_D->release_item_pickup(
                world_event_id, item, env);
            if (!release_ok) {
                msg_error(me, "这件奇遇物品的状态暂未保存，请稍后再试。\n",
                    "get", "world_event_persist_failed", item_name, 0, response_context);
                ITEM_FEEDBACK_D->sync_inventory_room_change(me, env, response_context);
                return 1;
            }
        }
        if (mapp(placement) && placement["error"] == "inventory_capacity_full")
            msg_error(me, "背包已满，装不下更多种类的物品。\n",
                "get", "inventory_capacity_full", item_name, 0, response_context);
        else
            msg_error(me, "你拿不动这样东西。\n", "get", "move_failed", item_name, 0,
                response_context);
        return 1;
    }

    if (world_event_reserved
        && (!objectp(placement["item"])
            || !WORLD_EVENT_D->commit_item_pickup(world_event_id,
                placement["item"], env))) {
        if (objectp(placement["item"]))
            WORLD_EVENT_D->release_item_pickup(world_event_id,
                placement["item"], env);
        rollback_world_event_placement(me, env, placement);
        msg_error(me, "这件奇遇物品的状态暂未保存，请稍后再试。\n",
            "get", "world_event_persist_failed", item_name, 0, response_context);
        ITEM_FEEDBACK_D->sync_inventory_room_change(me, env, response_context);
        return 1;
    }
    if (respawn_entity_id != "")
        WORLD_ENTITY_LIFECYCLE_D->record_item_pickup(env, respawn_entity_id);
    if (item_id != "")
        QUEST_D->record_item_acquired(me, item_id, placement["count"]);
    actor_name = query_actor_display_name(me, "有人");
    msg_system(me, "你捡起了" + display_name + "。\n", "item_pickup", response_context);
    msg_to_room(env, actor_name + "捡起了" + display_name + "。\n", ({ me }));
    ITEM_FEEDBACK_D->sync_inventory_room_change(me, env, response_context);
    return 1;
}

varargs int drop_item(object me, string arg, mapping response_context)
{
    object *items;
    object *success_items;
    object item;
    object env;
    int count;

    response_context = mapp(response_context) ? copy(response_context) : 0;
    if (!objectp(me) || !stringp(arg) || arg == "")
        return 0;

    env = environment(me);
    if (!objectp(env)) {
        msg_error(me, "你现在不能丢弃任何东西。\n", "drop", "missing_environment", arg, 0,
            response_context);
        return 1;
    }

    if (arg == "all") {
        items = all_inventory(me);
        if (sizeof(items) == 0) {
            msg_error(me, "你身上没有什么可丢弃的。\n", "drop", "empty_inventory", arg, 0,
                response_context);
            return 1;
        }
    } else {
        item = find_item_by_name(me, arg);
        if (!objectp(item)) {
            msg_error(me, "你身上没有「" + arg + "」。\n", "drop", "item_not_found", arg, 0,
                response_context);
            return 1;
        }
        items = ({ item });
    }

    success_items = ({ });
    count = 0;
    foreach (item in items) {
        if (is_equipped_item(item)) {
            msg_error(me, HIR + query_item_display_name(item) + "已经装备了，请先卸下。" NOR + "\n",
                "drop", "item_equipped", arg, 0, response_context);
            continue;
        }
        if (item->query("no_drop")) {
            msg_error(me, HIR + query_item_display_name(item) + "是绑定物品，无法丢弃。" NOR + "\n",
                "drop", "no_drop", arg, 0, response_context);
            continue;
        }
        if (item->move(env)) {
            if ((item->query("binding_policy") || "none") == "none")
                item->set("owner_id", "");
            success_items += ({ item });
            count++;
            msg_system(me, HIG + "你丢下了" + query_item_display_name(item) + "。" NOR + "\n",
                "item_drop", response_context);
            continue;
        }
        msg_error(me, HIY + query_item_display_name(item) + "无法丢弃。" NOR + "\n",
            "drop", "drop_failed", arg, 0, response_context);
    }

    if (count > 0) {
        ITEM_FEEDBACK_D->sync_inventory_room_change(me, env, response_context);
        return 1;
    }

    msg_error(me, "你什么也没丢下。\n", "drop", "no_item_dropped", arg, 0,
        response_context);
    return 1;
}
