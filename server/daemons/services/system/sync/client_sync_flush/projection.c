/**
 * @file    client_sync_flush/projection.c
 * @brief   投影刷新与 entry 匹配
 *
 * @par Description:
 * 含 {entry_projection_is_current, refresh_plan_projection_for_current, take_refreshed_plan_with_chat_retention, pending_entry_matches_player, pending_entry_matches_session, score_refresh_entry_matches_session, score_refresh_entry_matches_player, score_refresh_projection_is_current, retain_entry_chat_for_projection}。
 *
 * 本模块是 `client_sync_flush.c` 继承链的一环，由上层模块继承；
 * 跨模块调用经前向声明 + 运行时动态分派（this_object() 解析到最终对象）。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <system/xk_protocol.h>

inherit "/daemons/services/system/sync/client_sync_flush/packets";

mapping create_sync_plan();
mixed *entry_immediate_packets(mapping entry);
int entry_projection_is_current(object player, mapping entry);
mapping normalize_sync_plan(mapping plan);
mapping partition_immediate_packets(mixed packets);
int pending_entry_matches_player(string key, mapping entry,
    object player, string session_id);
int pending_entry_matches_session(string key, mapping entry,
    string session_id);
string query_object_session_id(object player);
string query_session_key();
mapping refresh_plan_projection_for_current(object player, mapping plan);
void remove_pending_chat_entry(string key);
void replace_entry_immediate_packets(mapping entry, mixed packets);
int retain_entry_chat_for_projection(object player, mapping entry,
    mapping projection);
void schedule_initial_chat_sync_flush();
int score_refresh_entry_matches_player();
int score_refresh_entry_matches_session(string key, mapping state,
    string session_id);
int score_refresh_projection_is_current(object player, mapping state);
void store_pending_entry(string key, mapping entry);
mapping take_refreshed_plan_with_chat_retention(object player,
    mapping entry, string key);

int entry_projection_is_current(object player, mapping entry)
{
    mapping projection;

    if (!mapp(entry))
        return 1;
    projection = mapp(entry["projection"])
        ? entry["projection"] : 0;
    if (!mapp(projection) && mapp(entry["plan"])
        && mapp(entry["plan"]["projection"]))
        projection = entry["plan"]["projection"];
    if (!mapp(projection))
        return 1;
    return LOGIN_PROJECTION_D->projection_identity_matches(player, projection);
}

mapping refresh_plan_projection_for_current(object player, mapping plan)
{
    mapping current_projection;

    if (!mapp(plan) || !mapp(plan["projection"]))
        return plan;
    if (LOGIN_PROJECTION_D->projection_identity_matches(player,
        plan["projection"]))
        return plan;
    current_projection = LOGIN_PROJECTION_D->query_projection_identity(player);
    plan["projection"] = copy(current_projection);
    return plan;
}

mapping take_refreshed_plan_with_chat_retention(object player,
    mapping entry, string key)
{
    mapping current_projection;
    mapping retained_plan;

    if (!mapp(entry) || entry_projection_is_current(player, entry))
        return 0;
    current_projection = LOGIN_PROJECTION_D->query_projection_identity(player);
    retained_plan = refresh_plan_projection_for_current(player,
        mapp(entry["plan"]) ? copy(entry["plan"]) : ([]));
    map_delete(_pending_sync_plans, key);
    remove_pending_chat_entry(key);
    if (retain_entry_chat_for_projection(entry["player"] || player,
        entry, current_projection) > 0) {
        store_pending_entry(key, entry);
        schedule_initial_chat_sync_flush();
    }
    return normalize_sync_plan(retained_plan);
}

int pending_entry_matches_player(string key, mapping entry,
    object player, string session_id)
{
    string object_key;

    if (!objectp(player) || !mapp(entry) || !stringp(key) || key == "")
        return 0;
    object_key = file_name(player);
    if (key == object_key)
        return 1;
    if (session_id != "" && key == query_session_key(session_id))
        return 1;
    if (objectp(entry["player"]) && entry["player"] == player)
        return 1;
    return session_id != "" && entry["session_id"] == session_id;
}

int pending_entry_matches_session(string key, mapping entry,
    string session_id)
{
    if (!stringp(session_id) || session_id == "")
        return 0;
    if (key == query_session_key(session_id))
        return 1;
    return mapp(entry) && entry["session_id"] == session_id;
}

int score_refresh_entry_matches_session(string key, mapping state,
    string session_id)
{
    if (!stringp(session_id) || session_id == "")
        return 0;
    if (key == query_session_key(session_id))
        return 1;
    return mapp(state) && state["session_id"] == session_id;
}

int score_refresh_entry_matches_player(string key, mapping state,
    object player, string session_id)
{
    string object_key;

    if (!objectp(player) || !stringp(key) || key == "")
        return 0;
    object_key = file_name(player);
    if (key == object_key)
        return 1;
    if (session_id != "" && key == query_session_key(session_id))
        return 1;
    if (mapp(state) && objectp(state["player"])
        && state["player"] == player)
        return 1;
    return mapp(state) && session_id != ""
        && state["session_id"] == session_id;
}

int score_refresh_projection_is_current(object player, mapping state)
{
    if (!mapp(state) || !mapp(state["projection"]))
        return 0;
    return LOGIN_PROJECTION_D->projection_identity_matches(player,
        state["projection"]);
}

int retain_entry_chat_for_projection(object player, mapping entry,
    mapping projection)
{
    mapping partition;
    mixed *chat;

    if (!mapp(entry))
        return 0;
    partition = partition_immediate_packets(entry_immediate_packets(entry));
    chat = arrayp(partition["chat"]) ? partition["chat"] : ({ });
    entry["player"] = player;
    entry["session_id"] = query_object_session_id(player);
    entry["projection"] = mapp(projection)
        ? copy(projection) : LOGIN_PROJECTION_D->query_projection_identity(player);
    entry["plan"] = create_sync_plan();
    replace_entry_immediate_packets(entry, chat);
    if (sizeof(chat) > 0)
        entry["chat_only"] = 1;
    else
        map_delete(entry, "chat_only");
    return sizeof(chat);
}
