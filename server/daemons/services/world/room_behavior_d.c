/**
 * @file    room_behavior_d.c
 * @brief   房间行为守护进程,管理房间行为档案与动作分发。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

nosave mapping _room_behaviors;

int apply_room_behavior_action(object room, object player, string behavior_id, string verb, string arg);
int apply_room_behavior_leave(object room, object player, string behavior_id, string dir);
private int apply_plum_stakes_action(object player, mapping action, string arg);
private int apply_quiet_meditation_action(object player, mapping action, string arg);

private mapping query_behavior_config()
{
    mapping config;

    config = CONFIG_D->query_full_config("content");
    return mapp(config) ? config : ([]);
}

private void load_behavior_config()
{
    mapping config;

    config = query_behavior_config();
    _room_behaviors = mapp(config["room_behaviors"]) ? copy(config["room_behaviors"]) : ([ ]);
}

void on_config_reloaded(string key, string config_name)
{
    if (key != "content" && config_name != "content")
        return;
    load_behavior_config();
}

void on_config_reload_rollback(string key, string config_name)
{
    if (key != "content" && config_name != "content")
        return;
    load_behavior_config();
}

void create()
{
    ::create();
    CONFIG_D->compat_register_config_callback("content", "on_config_reloaded", this_object());
    if (!mapp(_room_behaviors))
        _room_behaviors = ([ ]);
    load_behavior_config();
}

mapping query_room_behavior(string behavior_id)
{
    mapping behavior;

    if (!stringp(behavior_id) || behavior_id == "")
        return 0;
    behavior = _room_behaviors[behavior_id];
    return mapp(behavior) ? copy(behavior) : 0;
}

mapping query_room_behaviors_for_room(string room_path)
{
    mapping result;
    mapping behavior;
    string behavior_id;

    result = ([ ]);
    if (!stringp(room_path) || room_path == "")
        return result;
    foreach (behavior_id, behavior in _room_behaviors) {
        if (!mapp(behavior) || behavior["room"] != room_path)
            continue;
        result[behavior_id] = copy(behavior);
    }
    return result;
}

int dispatch_room_command(object player, object room, string verb, string arg)
{
    mapping behaviors;
    mapping behavior;
    string behavior_id;

    if (!objectp(player) || !objectp(room) || !stringp(verb) || verb == "")
        return 0;
    behaviors = query_room_behaviors_for_room(base_name(room));
    foreach (behavior_id, behavior in behaviors) {
        if (apply_room_behavior_action(room, player, behavior_id, verb, arg))
            return 1;
    }
    return 0;
}

int dispatch_room_leave(object player, object room, string dir)
{
    mapping behaviors;
    mapping behavior;
    string behavior_id;

    if (!objectp(player) || !objectp(room) || !stringp(dir) || dir == "")
        return 0;
    behaviors = query_room_behaviors_for_room(base_name(room));
    foreach (behavior_id, behavior in behaviors) {
        if (apply_room_behavior_leave(room, player, behavior_id, dir))
            return 1;
    }
    return 0;
}

int apply_room_behavior_action(object room, object player, string behavior_id, string verb, string arg)
{
    mapping behavior;
    mapping actions;
    mapping action;

    if (!objectp(room) || !objectp(player) || !stringp(behavior_id) || behavior_id == "" || !stringp(verb) || verb == "")
        return 0;
    behavior = query_room_behavior(behavior_id);
    if (!mapp(behavior))
        return 0;
    actions = mapp(behavior["actions"]) ? behavior["actions"] : ([ ]);
    action = actions[verb];
    if (!mapp(action))
        return 0;

    switch (action["type"] || "") {
        case "plum_stakes":
            return apply_plum_stakes_action(player, action, arg);
        case "quiet_meditation":
            return apply_quiet_meditation_action(player, action, arg);
    }
    return 0;
}

int apply_room_behavior_leave(object room, object player, string behavior_id, string dir)
{
    mapping behavior;
    mapping action;
    mixed positive_dirs;
    string count_temp;
    int handled;
    int delta;

    if (!objectp(room) || !objectp(player) || !stringp(behavior_id) || behavior_id == "" || !stringp(dir) || dir == "")
        return 0;
    behavior = query_room_behavior(behavior_id);
    if (!mapp(behavior) || !mapp(behavior["actions"]))
        return 0;
    handled = 0;
    foreach (action in values(behavior["actions"])) {
        if (!mapp(action))
            continue;
        if (stringp(action["leave_count_temp"]) && action["leave_count_temp"] != "") {
            positive_dirs = arrayp(action["leave_positive_dirs"]) ? action["leave_positive_dirs"] : ({ });
            count_temp = action["leave_count_temp"];
            delta = member_array(dir, positive_dirs) >= 0
                ? (intp(action["leave_positive_delta"]) ? action["leave_positive_delta"] : 1)
                : (intp(action["leave_negative_delta"]) ? action["leave_negative_delta"] : -1);
            player->add_temp(count_temp, delta);
            handled = 1;
        }
        if ((action["leave_dir"] || "") != dir)
            continue;
        if ((action["leave_reset_temp"] || "") != "")
            player->delete_temp(action["leave_reset_temp"]);
        handled = 1;
    }
    return handled;
}

private int apply_plum_stakes_action(object player, mapping action, string arg)
{
    string prompt;
    int min_skill;

    prompt = stringp(action["prompt"]) ? action["prompt"] : "梅花桩";
    min_skill = intp(action["enter_min_skill"]) ? action["enter_min_skill"] : 30;
    if (!stringp(arg) || arg == "")
        arg = prompt;
    if (player->query_temp("meihua/on")) {
        if (arg == "down") {
            player->delete_temp("meihua/on");
            message_vision("$N轻轻一纵，从梅花桩上落回地面。\n", player);
            return 1;
        }
        msg_send(player, "你已经站在梅花桩上了，再乱动只会失足。\n");
        return 1;
    }
    if (arg == "down") {
        msg_send(player, "你本来就站在地上。\n");
        return 1;
    }
    if ((int)player->query_skill("dodge", 1) < min_skill) {
        message_vision(stringp(action["enter_fail_message"]) ? action["enter_fail_message"] : "$N想跃上梅花桩，却脚下一滑，摔得一阵狼狈。\n", player);
        if (intp(action["enter_fail_qi"]) && action["enter_fail_qi"] > 0)
            player->receive_wound("qi", action["enter_fail_qi"], player);
        return 1;
    }
    message_vision(stringp(action["enter_message"]) ? action["enter_message"] : "$N提气一纵，稳稳落在梅花桩上。\n", player);
    player->set_temp("meihua/on", 1);
    return 1;
}

private int apply_quiet_meditation_action(object player, mapping action, string arg)
{
    string required_family;

    required_family = stringp(action["family"]) ? action["family"] : "";
    if (player->is_busy()) {
        msg_send(player, "你正忙着，先静下心来再说。\n");
        return 1;
    }
    if (required_family != "" && player->query("family/family_name") != required_family)
        return 0;
    msg_send(player, stringp(action["message"]) && action["message"] != "" ? action["message"] : "你静坐片刻，心绪沉静下来。\n");
    if (intp(action["busy"]) && action["busy"] > 0)
        player->start_busy(action["busy"]);
    return 1;
}
