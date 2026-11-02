/**
 * @file    util_player.c
 * @brief   玩家交互工具 - helper utility



 *
 * @par Description:
 * helper utility，提供玩家/生物查找、邻近交互目标解析与轻量展示辅助。
 * 本文件只负责复用性 helper，不承载玩家状态真源或战斗/登录规则。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/ansi.h>
#include <system/constants.h>
#include <system/daemon.h>
#include <system/services.h>
#include <system/errors.h>
#include <game_constants.h>
#include <framework/message_facade.h>

string get_object_display_name(object ob, string fallback);
string get_object_display_id(object ob, string fallback);

private object *query_online_players_current()
{
    object *online_players;

    online_players = users();
    return arrayp(online_players) ? online_players : ({ });
}

private int player_matches_name(object user, string player_name)
{
    string account_id;
    string display_name;
    string display_id;

    if (!objectp(user) || !stringp(player_name) || player_name == "")
        return 0;
    account_id = function_exists("query_account_id", user)
        ? (user->query_account_id() || "") : "";
    display_name = get_object_display_name(user, "");
    display_id = get_object_display_id(user, "");
    return player_name == account_id
        || player_name == get_object_id(user)
        || player_name == display_id
        || player_name == display_name;
}

/**
 * @brief 查找在线玩家
 * @param player_name 玩家名称
 * @return 玩家对象，找不到返回 0
 */
object find_player_online(string player_name)
{
    object* users_list;
    object user;

    if (!stringp(player_name) || player_name == "")
        return 0;

    users_list = query_online_players_current();

    foreach (user in users_list) {
        if (player_matches_name(user, player_name))
            return user;
    }

    return 0;
}

object find_visible_player(object me, string target_name)
{
    object env;
    object target;
    object *inv;
    string account_id;
    string display_name;
    string display_id;

    if (!objectp(me) || !stringp(target_name) || target_name == "")
        return 0;

    env = environment(me);
    if (!objectp(env))
        return 0;

    target = present(target_name, env);
    if (objectp(target) && target != me) {
        account_id = "";
        if (function_exists("query_account_id", target))
            account_id = target->query_account_id() || "";
        display_name = get_object_display_name(target, "");
        display_id = get_object_display_id(target, "");

        if (account_id != "" || userp(target)) {
            if (target_name == account_id || target_name == display_name || target_name == display_id)
                return target;
        }
    }

    inv = all_inventory(env);
    foreach (target in inv) {
        if (!objectp(target) || target == me)
            continue;

        account_id = "";
        if (function_exists("query_account_id", target))
            account_id = target->query_account_id() || "";
        if (account_id == "" && !userp(target))
            continue;

        display_name = get_object_display_name(target, "");
        display_id = get_object_display_id(target, "");
        if (target_name == account_id || target_name == display_name || target_name == display_id)
            return target;
    }

    return 0;
}

object *query_online_players(int require_environment)
{
    object *users_list;

    users_list = query_online_players_current();
    if (!arrayp(users_list))
        return ({ });

    if (!require_environment)
        return users_list;

    return filter_array(users_list, (: objectp($1) && environment($1) :));
}

mapping query_online_player_runtime_snapshot(int require_environment)
{
    object *users_list;
    object player;
    string *player_ids;
    string *wizard_ids;
    string player_id;

    users_list = query_online_players(require_environment);
    player_ids = ({ });
    wizard_ids = ({ });

    foreach (player in users_list) {
        player_id = get_object_display_id(player, "");
        if (!stringp(player_id) || player_id == "")
            player_id = get_object_display_name(player, "未知");

        if (wizardp(player))
            wizard_ids += ({ player_id });
        else
            player_ids += ({ player_id });
    }

    return ([
        "total": sizeof(users_list),
        "wizard_count": sizeof(wizard_ids),
        "player_count": sizeof(player_ids),
        "wizard_ids": wizard_ids,
        "player_ids": player_ids,
    ]);
}

string get_object_display_name(object ob, string fallback)
{
    string name;

    if (!objectp(ob))
        return stringp(fallback) && fallback != "" ? fallback : "";

    name = get_object_name(ob);
    if (!stringp(name) || name == "")
        name = stringp(fallback) && fallback != "" ? fallback : "";

    return name;
}

string get_object_display_id(object ob, string fallback)
{
    string id;

    if (!objectp(ob))
        return stringp(fallback) && fallback != "" ? fallback : "";

    id = get_object_id(ob);
    if (!stringp(id) || id == "")
        id = stringp(fallback) && fallback != "" ? fallback : "";

    return id;
}

string get_object_short_name(object ob, string fallback)
{
    string name;

    if (!objectp(ob))
        return stringp(fallback) && fallback != "" ? fallback : "";

    if (function_exists("query_short", ob))
        name = ob->query_short();
    if (!stringp(name) || name == "") {
        if (function_exists("short", ob))
            name = ob->short();
    }
    if (!stringp(name) || name == "")
        name = ob->query("short");
    if (!stringp(name) || name == "")
        name = get_object_display_name(ob, fallback);

    return name;
}

mapping query_player_public_snapshot(object player)
{
    mapping progression;
    int progression_rank;
    int skill_cap;
    int exp;
    int idle_sec;
    int last_on;

    if (!objectp(player))
        return ([
            "name": "无名",
            "id": "未知",
            "progression_rank": 1,
            "skill_cap": 0,
            "exp": 0,
            "idle": 0,
            "last_on": time(),
        ]);

    progression = NUMERIC_RULE_D->query_player_progression_snapshot(player);
    progression_rank = mapp(progression)
        && intp(progression["progression_rank"])
        ? progression["progression_rank"] : 1;
    skill_cap = mapp(progression) && intp(progression["skill_cap"])
        ? progression["skill_cap"] : 0;
    exp = player->query("combat_exp") || 0;
    idle_sec = query_idle(player);
    last_on = player->query("last_on") || time();

    return ([
        "name": get_object_display_name(player, "无名"),
        "id": get_object_display_id(player, "未知"),
        "progression_rank": progression_rank,
        "skill_cap": skill_cap,
        "exp": exp,
        "idle": idle_sec,
        "last_on": last_on,
    ]);
}

string format_player_online_label(int is_online, int last_on)
{
    if (is_online)
        return HIG "在线" NOR;

    return YEL + ctime(last_on)[0..9] + NOR;
}

string format_player_idle_label(int idle_sec)
{
    if (idle_sec > 300)
        return HIY "[发呆]" NOR;

    return "";
}

string format_player_brief_line(mapping snapshot)
{
    string name;
    string id;
    int progression_rank;

    if (!mapp(snapshot))
        return "无名(未知) 技能上限0";

    name = stringp(snapshot["name"]) && snapshot["name"] != "" ? snapshot["name"] : "无名";
    id = stringp(snapshot["id"]) && snapshot["id"] != "" ? snapshot["id"] : "未知";
    progression_rank = intp(snapshot["progression_rank"]) ? snapshot["progression_rank"] : 1;

    return sprintf("%s(%s) 档位%d", name, id, progression_rank);
}

string format_tagged_player_brief_line(string tag, mapping snapshot)
{
    string prefix;

    prefix = stringp(tag) ? tag : "";
    return prefix + format_player_brief_line(snapshot);
}

/**
 * @brief 查找房间中的生物
 * @param env 环境对象
 * @param name 生物名称
 * @param exclude 排除的对象
 * @return 生物对象，找不到返回 0
 */
object find_living_in_room(object env, string name, object exclude)
{
    object* obs;
    object ob;
    object candidate;
    string ob_name;
    string ob_id;

    if (!env || !name || name == "")
        return 0;

    candidate = ROOM_VIEW_D->query_indexed_visible_living(env, name, exclude);
    if (objectp(candidate))
        return candidate;

    obs = all_inventory(env);

    foreach(ob in obs) {
        if (ob == exclude)
            continue;

        if (!living(ob) && !(function_exists("is_character", ob) && ob->is_character()))
            continue;

        if (objectp(exclude) && QUEST_ACTOR_D->should_hide_shared_npc(exclude, ob, env))
            continue;

        ob_id = get_object_display_id(ob, "");
        ob_name = get_object_display_name(ob, "");

        if (ob_id == name || ob_name == name)
            return ob;

        if (function_exists("id", ob) && ob->id(name))
            return ob;
    }

    return 0;
}

object find_living_in_room_by_contract(object env, string npc_type,
    string flag, object exclude)
{
    object *obs;
    object ob;

    if (!objectp(env))
        return 0;

    obs = all_inventory(env);
    foreach (ob in obs) {
        if (!objectp(ob) || ob == exclude)
            continue;
        if (!living(ob) && !(function_exists("is_character", ob) && ob->is_character()))
            continue;
        if (objectp(exclude) && QUEST_ACTOR_D->should_hide_shared_npc(exclude, ob, env))
            continue;
        if (stringp(npc_type) && npc_type != "" && ob->query("npc_type") == npc_type)
            return ob;
        if (stringp(flag) && flag != "" && ob->query(flag))
            return ob;
    }

    return 0;
}

object find_player_in_room(object me, string target_name)
{
    object env;
    object target;
    object *inv;
    string account_id;
    string display_name;
    string display_id;

    if (!objectp(me) || !stringp(target_name) || target_name == "")
        return 0;

    env = environment(me);
    if (!objectp(env))
        return 0;

    inv = all_inventory(env);
    foreach (target in inv) {
        if (!objectp(target) || target == me || !userp(target))
            continue;

        account_id = "";
        if (function_exists("query_account_id", target))
            account_id = target->query_account_id() || "";

        display_name = get_object_display_name(target, "");
        display_id = get_object_display_id(target, "");
        if (target_name == account_id || target_name == display_name || target_name == display_id)
            return target;

        if (function_exists("id", target) && target->id(target_name))
            return target;
    }

    return 0;
}

/**
 * @brief 解析对话目标（同房可交互对象）
 * @param me 玩家对象
 * @param arg 目标参数
 * @return 目标对象，失败返回0并提示错误
 */
object resolve_talk_target(object me, string arg)
{
    object env;
    object target;

    if (!objectp(me))
        return 0;

    if (!stringp(arg) || arg == "")
        return 0;

    env = environment(me);
    if (!objectp(env))
        return 0;

    target = find_visible_player(me, arg);
    if (!objectp(target))
        target = find_living_in_room(env, arg, me);

    if (!objectp(target))
        return 0;

    if (userp(target))
        return target;

    if (function_exists("is_character", target) && target->is_character())
        return target;

    if (function_exists("do_talk", target) || function_exists("do_ask", target))
        return target;

    return 0;

}

/**
 * @brief 执行玩家移动逻辑（方向解析/房间解析/广播/视图刷新）
 * @param me 玩家对象
 * @param raw_dir 原始方向参数
 * @return mapping (['ok':1/0, 'error_message':'', 'error_type':''])
 */
mapping move_player_by_direction(object me, string raw_dir)
{
    mapping result = (["ok": 0, "error_message": "", "error_type": ""]);
    object env;
    mapping travel_result;
    mapping exits;
    string std_dir;
    string dest;
    int location_generation;
    int move_started_at;
    int move_cpu_started_at;
    int move_cpu_finished_at;

    if (!objectp(me))
        return result;

    if (me->is_fighting()) {
        if (!COMBAT_D->is_in_combat(me)) {
            me->delete_temp("fight_ob");
            me->delete_temp("enemies");
        } else {
            object before_room;
            mixed before_generation;

            std_dir = call_other("/lib/std/utils/util_direction",
                "normalize_direction", raw_dir);
            /* L-114：逃跑是否成功必须以真实位置变化为准，不能因为命令被
             * 接受就回报成功。attempt_flee 用消息表达失败，这里用环境与
             * 位置代数的前后对比判定（同一判定口径与普通移动一致）。 */
            before_room = environment(me);
            before_generation = me->query_temp("world/location_generation");
            COMBAT_D->attempt_flee(me,
                stringp(std_dir) && std_dir != "" ? std_dir : raw_dir);
            if (environment(me) != before_room
                || me->query_temp("world/location_generation") != before_generation) {
                result["ok"] = 1;
                return result;
            }
            result["error_message"] = "你没能逃离战斗，位置没有变化。\n";
            result["error_type"] = "not_found";
            return result;
        }
    }

    std_dir = call_other("/lib/std/utils/util_direction", "normalize_direction", raw_dir);

    if (me->is_busy()) {
        result["error_message"] = "你正忙着呢。\n";
        result["error_type"] = "invalid_param";
        return result;
    }

    env = environment(me);
    if (!objectp(env)) {
        result["error_message"] = "你好像不在任何地方。\n";
        result["error_type"] = "not_found";
        return result;
    }

    exits = env->query("exits");
    if ((!stringp(std_dir) || std_dir == "") && mapp(exits) && stringp(raw_dir)
        && regexp(raw_dir, "^[a-z][a-z0-9_]*$") && stringp(exits[raw_dir]) && exits[raw_dir] != "")
        std_dir = raw_dir;
    if (!stringp(std_dir) || std_dir == "") {
        result["error_message"] = "没有这个方向。可用方向：n/s/e/w/ne/nw/se/sw/u/d/nu/nd/su/sd/eu/ed/wu/wd/enter/out，或当前房间列出的其他出口名\n";
        result["error_type"] = "invalid_param";
        return result;
    }
    if (!mapp(exits) || !stringp(exits[std_dir]) || exits[std_dir] == "") {
        result["error_message"] = "这个方向没有出路。\n";
        result["error_type"] = "not_found";
        return result;
    }

    dest = exits[std_dir];
    location_generation = me->query_temp("world/location_generation");
    move_started_at = perf_counter_ns();
    move_cpu_started_at = thread_cpu_time_ns();
    travel_result = WORLD_TRAVEL_D->move_actor(me, dest, ([
        "reason": "direction",
        "direction": std_dir,
        "expected_location_generation": intp(location_generation)
            ? location_generation : 0,
    ]));
    if (!mapp(travel_result) || !travel_result["ok"]) {
        move_cpu_finished_at = thread_cpu_time_ns();
        GATEWAY_TELEMETRY_D->record_gateway_movement_phase_timing(
            me, "framework_total", perf_counter_ns() - move_started_at,
            move_cpu_started_at >= 0 && move_cpu_finished_at >= move_cpu_started_at
                ? move_cpu_finished_at - move_cpu_started_at : -1);
        result["error_message"] = mapp(travel_result)
            && stringp(travel_result["error"])
            ? travel_result["error"] + "\n" : "移动失败。\n";
        result["error_type"] = "not_found";
        return result;
    }
    move_cpu_finished_at = thread_cpu_time_ns();
    GATEWAY_TELEMETRY_D->record_gateway_movement_phase_timing(
        me, "framework_total", perf_counter_ns() - move_started_at,
        move_cpu_started_at >= 0 && move_cpu_finished_at >= move_cpu_started_at
            ? move_cpu_finished_at - move_cpu_started_at : -1);

    result["ok"] = 1;
    return result;
}
