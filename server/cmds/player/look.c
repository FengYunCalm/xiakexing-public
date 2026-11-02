/**
 * @file    look.c
 * @brief   查看命令 - 查看房间环境和物品 (Facade)



 *
 * @par Description:
 * 查看命令Facade，整合所有查看功能模块：
 * - look_room.c: 查看房间
 * - look_living.c: 查看人物
 * - look_hints.c: 互动提示生成
 *
 * @par Architecture:
 * @code
 *   look.c (Facade)
 *     │
 *     ├── look_room.c    - 房间环境查看
 *     ├── look_living.c  - 人物观察、成长修为
 *     └── look_hints.c   - 互动提示、外观描述
 * @endcode
 */

#pragma strict_types

#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";
inherit __DIR__ "look_hints";
inherit __DIR__ "look_living";
inherit __DIR__ "look_room";

/* Function prototypes */
int main(object me, string arg);
void look_object(object me, object ob, mapping response_context);
void look_item(object me, object ob, mapping response_context);
void look_living(object me, object ob);
int help(object me);

private int is_room_scoped_target(string arg)
{
    return stringp(arg) && strlen(arg) > 5 && arg[0..4] == "room:";
}

private string strip_room_scope(string arg)
{
    return is_room_scoped_target(arg) ? arg[5..] : arg;
}

private mapping *append_item_action(mapping *actions, string label, string cmd)
{
    mapping action;

    if (!arrayp(actions))
        actions = ({ });
    if (!stringp(label) || label == "" || !stringp(cmd) || cmd == "")
        return actions;

    foreach (action in actions) {
        if (mapp(action) && action["cmd"] == cmd)
            return actions;
    }

    return actions + ({ ([
        "label": label,
        "cmd": cmd,
    ]) });
}

private string resolve_item_command_target(object ob)
{
    string target;

    target = call_other("/lib/std/utils/util_item", "get_item_command_target", ob);
    return stringp(target) ? target : "";
}

private string resolve_item_consume_action(object ob, string item_type)
{
    string consume_action;

    if (!objectp(ob))
        return "";

    consume_action = ob->query("consume_action") || "";
    if (consume_action != "")
        return consume_action;
    if ((ob->query("liquid/remaining") || 0) > 0 || (ob->query("liquid/supply") || 0) > 0)
        return "drink";
    if ((ob->query("food_remaining") || 0) > 0 || (ob->query("food_supply") || 0) > 0)
        return "eat";
    if (item_type == "consumable")
        return "eat";
    return "";
}

private int item_supports_direct_consume(object ob, string item_type)
{
    return resolve_item_consume_action(ob, item_type) != "";
}

private mapping *build_item_inspect_actions(object me, object ob, string command_target)
{
    mapping *actions;
    string item_type;
    string consume_action;
    string room_command_target;
    int equipped;

    actions = ({ });
    if (!objectp(me) || !objectp(ob) || !stringp(command_target) || command_target == "")
        return actions;

    item_type = function_exists("query_item_type", ob) ? ob->query_item_type() : (ob->query("item_type") || "misc");
    consume_action = resolve_item_consume_action(ob, item_type);
    equipped = function_exists("query_equipped", ob) && ob->query_equipped();
    room_command_target = "room:" + command_target;

    if (environment(ob) == me) {
        if (equipped)
            actions = append_item_action(actions, "卸下", "unequip " + command_target);
        else if (item_type == "weapon" || item_type == "armor"
            || function_exists("wield", ob) || function_exists("wear", ob))
            actions = append_item_action(actions, "装备", "equip " + command_target);

        if (item_supports_direct_consume(ob, item_type)) {
            if (consume_action == "drink")
                actions = append_item_action(actions, "饮用", "drink " + command_target);
            else
                actions = append_item_action(actions, "使用", "eat " + command_target);
        }

        if (!equipped)
            actions = append_item_action(actions, "丢弃", "drop " + command_target);
        return actions;
    }

    if (environment(ob) == environment(me)) {
        if (ob->query("exploration_chest_id") || item_type == "exploration_chest")
            actions = append_item_action(actions, "开启", "open " + command_target);
        if (!ob->query("no_get"))
            actions = append_item_action(actions, "拾取", "get " + room_command_target);
    }

    return actions;
}

private mapping build_item_inspect_target(object me, object ob, string content)
{
    mapping target;
    string command_target;
    string item_type;
    string sub_type;
    string consume_action;
    int quantity;
    int equipped;

    command_target = resolve_item_command_target(ob);
    item_type = function_exists("query_item_type", ob) ? ob->query_item_type() : (ob->query("item_type") || "misc");
    sub_type = ob->query("sub_type") || ob->query("armor_type") || "";
    quantity = function_exists("query_amount", ob) ? ob->query_amount() : (ob->query("amount") || ob->query("count") || 1);
    if (!intp(quantity) || quantity <= 0)
        quantity = 1;
    equipped = function_exists("query_equipped", ob) && ob->query_equipped();
    consume_action = resolve_item_consume_action(ob, item_type);

    target = ([
        "id": command_target,
        "name": call_other("/lib/std/utils/util_item", "get_item_display_name", ob, "物品"),
        "description": stringp(content) ? content : "",
        "type": item_type,
        "sub_type": sub_type,
        "quantity": quantity,
        "can_use": item_supports_direct_consume(ob, item_type) ? 1 : 0,
        "can_equip": (item_type == "weapon" || item_type == "armor"
            || function_exists("wield", ob) || function_exists("wear", ob)) ? 1 : 0,
        "equipped": equipped ? 1 : 0,
        "location": environment(ob) == me ? (equipped ? "equipped" : "inventory") : "room",
    ]);
    if (target["can_use"])
        target["consume_action"] = consume_action;
    if (stringp(ob->query("template_id")) && ob->query("template_id") != "")
        target["template_id"] = ob->query("template_id");

    return target;
}

private void emit_item_inspect_dialog(object me, object ob, string content,
    mapping response_context)
{
    string command_target;
    string dialog_id;
    string title;

    if (!objectp(me) || !objectp(ob))
        return;

    command_target = resolve_item_command_target(ob);
    dialog_id = stringp(command_target) && command_target != ""
        ? "look_item:" + command_target
        : "look_item";
    title = call_other("/lib/std/utils/util_item", "get_item_display_name", ob, "物品");
    xk_send_dialog(me,
        title,
        content,
        DIALOG_PROJECTION_D->build_dialog_extra("look_item", dialog_id, ([
            "target": build_item_inspect_target(me, ob, content),
            "actions": build_item_inspect_actions(me, ob, command_target),
        ])), response_context);
}

private string resolve_object_display_text(object ob)
{
    string str;

    if (!objectp(ob))
        return "你看到了一样东西。";

    if (function_exists("query_item_info", ob))
        str = ob->query_item_info();

    if (!stringp(str) || str == "")
        str = call_other("/lib/std/utils/util_player", "get_object_short_name", ob, "");

    if (!stringp(str) || str == "")
        str = call_other("/lib/std/utils/util_player", "get_object_display_name", ob, "你看到了一样东西。");

    return str;
}

/**
 * @brief 主函数
 */
int main(object me, string arg)
{
    object env, ob;
    string lookup_arg;
    mapping response_context;
    int room_scoped;

    if (!me) return 0;

    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    env = environment(me);

    if (!arg || arg == "") {
        look_room(me, env, response_context);
        QUEST_D->record_look_progress(me, env);
    } else {
        room_scoped = is_room_scoped_target(arg);
        lookup_arg = strip_room_scope(arg);
        if (room_scoped) {
            ob = present(lookup_arg, env);
            if (!objectp(ob))
                ob = call_other("/lib/std/utils/util_item", "find_item_in_collection",
                    all_inventory(env), lookup_arg);
        } else {
            ob = present(lookup_arg, me);
            if (!objectp(ob))
                ob = find_living_in_room(env, lookup_arg, me);
            if (!objectp(ob))
                ob = present(lookup_arg, env);
            if (!objectp(ob))
                ob = call_other("/lib/std/utils/util_item", "find_item_in_collection",
                    all_inventory(me), lookup_arg);
            if (!objectp(ob))
                ob = call_other("/lib/std/utils/util_item", "find_item_in_collection",
                    all_inventory(env), lookup_arg);
        }
        if (objectp(ob) && (living(ob)
            || (function_exists("is_character", ob) && call_other(ob, "is_character"))
            || (function_exists("is_npc", ob) && call_other(ob, "is_npc"))
            || (function_exists("is_player", ob) && call_other(ob, "is_player")))
            && QUEST_ACTOR_D->should_hide_shared_npc(me, ob, env)) {
            ob = 0;
        }
        if (!objectp(ob))
            ob = room_scoped ? 0 : QUEST_ACTOR_D->resolve_actor_target(me, env, lookup_arg);
        if (objectp(ob) && environment(ob) != env && environment(ob) != me)
            ob = 0;
        if (ob) {
            look_object(me, ob, response_context);
        } else {
            msg_send(me, "这里没有 " + lookup_arg + "\n");
        }
    }

    return 1;
}

/**
 * @brief 查看对象（分发到living或item）
 */
void look_object(object me, object ob, mapping response_context)
{
    if (!me || !ob) return;

    if (living(ob) ||
        (function_exists("is_character", ob) && call_other(ob, "is_character")) ||
        (function_exists("is_npc", ob) && call_other(ob, "is_npc")) ||
        (function_exists("is_player", ob) && call_other(ob, "is_player"))) {
        look_living(me, ob);
        QUEST_D->record_look_progress(me, ob);
        return;
    }

    look_item(me, ob, response_context);
}

/**
 * @brief 查看物品
 */
void look_item(object me, object ob, mapping response_context)
{
    string str;

    if (!me || !ob) return;

    str = resolve_object_display_text(ob);

    if (!is_xk_client(me))
        msg_send(me, str + "\n", response_context);
    emit_item_inspect_dialog(me, ob, str, response_context);
    QUEST_D->record_item_inspection(me, ob);
    QUEST_D->record_look_progress(me, ob);
}

/**
 * @brief 帮助信息
 */
int help(object me)
{
    msg_send(me, @HELP
指令格式：look [对象]

查看当前环境或特定对象。

示例：
  look        - 查看当前房间
  look table  - 查看桌子
  look man    - 查看某人

HELP
    );
    return 1;
}
