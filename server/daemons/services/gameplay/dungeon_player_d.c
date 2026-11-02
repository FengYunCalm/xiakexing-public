/**
 * @file    dungeon_player_d.c
 * @brief   副本玩家级持久化真源



 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>

inherit "/lib/framework/daemon";

mapping _player_records;
nosave mapping _last_dungeon_save_phase;

private string get_save_file()
{
    return DATA_DIR + "runtime/dungeon_player_d";
}

private void ensure_runtime_state()
{
    if (!mapp(_player_records))
        _player_records = ([]);
}

private void restore_player_data()
{
    string file;

    file = get_save_file();
    if (file_size(file + ".o") <= 0) {
        _player_records = ([]);
        return;
    }

    if (catch(restore_object(file))) {
        _player_records = ([]);
        return;
    }

    ensure_runtime_state();
}

private int persist_player_data()
{
    string file;
    string runtime_dir;
    mapping detail;

    ensure_runtime_state();
    file = get_save_file();
    runtime_dir = DATA_DIR + "runtime";
    if (file_size(runtime_dir) == -1)
        mkdir(runtime_dir);

    detail = owner_save_object_phases(file, "owner/global/service/dungeon/player", "dungeon_player_state", ([
        "player_record_count": sizeof(_player_records),
        "state_digest": hash("sha256", save_variable(_player_records)),
    ]), "dungeon_player/save/" + hash("sha256", save_variable(_player_records)));
    _last_dungeon_save_phase = mapp(detail) ? copy(detail)
        : ([ "outcome": "unknown", "persisted": -1, "save_result": 0,
            "error": "dungeon owner save result unavailable" ]);
    if (!mapp(detail) || detail["persisted"] != 1)
        return 0;
    return detail["ok"] == 1 ? 1 : 2;
}

int save_player_data()
{
    return persist_player_data();
}

mapping query_dungeon_save_phase()
{
    return mapp(_last_dungeon_save_phase) ? copy(_last_dungeon_save_phase)
        : ([ "outcome": "not_attempted", "persisted": 0, "save_result": 0 ]);
}

private mapping ensure_player_record(string player_id)
{
    mapping record;

    if (!stringp(player_id) || player_id == "")
        return 0;

    ensure_runtime_state();
    record = _player_records[player_id];
    if (!mapp(record))
        record = ([]);
    if (!mapp(record["cooldowns"]))
        record["cooldowns"] = ([]);
    if (!mapp(record["resume_context"]))
        record["resume_context"] = ([]);
    _player_records[player_id] = record;
    return record;
}

int query_player_cooldown(string player_id, string dungeon_type)
{
    mapping record;
    mixed previous_value;
    int expire_time;
    int remaining;
    int save_result;

    if (!stringp(player_id) || player_id == "" || !stringp(dungeon_type) || dungeon_type == "")
        return 0;

    record = ensure_player_record(player_id);
    expire_time = record["cooldowns"][dungeon_type];
    if (!intp(expire_time) || expire_time <= 0)
        return 0;

    remaining = expire_time - time();
    if (remaining <= 0) {
        previous_value = record["cooldowns"][dungeon_type];
        map_delete(record["cooldowns"], dungeon_type);
        save_result = save_player_data();
        if (save_result == 0 && mapp(_last_dungeon_save_phase)
            && _last_dungeon_save_phase["outcome"] == "not_written")
            record["cooldowns"][dungeon_type] = previous_value;
        return 0;
    }

    return remaining;
}

int set_player_cooldown_until(string player_id, string dungeon_type,
    int expire_time)
{
    mapping record;
    mixed previous_value;
    int save_result;

    if (!stringp(player_id) || player_id == "" || !stringp(dungeon_type)
        || dungeon_type == "" || expire_time <= time())
        return 0;

    record = ensure_player_record(player_id);
    previous_value = record["cooldowns"][dungeon_type];
    record["cooldowns"][dungeon_type] = expire_time;
    save_result = save_player_data();
    if (save_result == 0 && mapp(_last_dungeon_save_phase)
        && _last_dungeon_save_phase["outcome"] == "not_written") {
        if (undefinedp(previous_value))
            map_delete(record["cooldowns"], dungeon_type);
        else
            record["cooldowns"][dungeon_type] = previous_value;
    }
    return save_result;
}

int set_player_cooldown(string player_id, string dungeon_type, int cooldown)
{
    if (cooldown <= 0)
        return 0;
    return set_player_cooldown_until(player_id, dungeon_type,
        time() + cooldown);
}

mapping query_resume_context(string player_id)
{
    mapping record;

    if (!stringp(player_id) || player_id == "")
        return 0;

    record = ensure_player_record(player_id);
    if (!mapp(record["resume_context"]) || !sizeof(record["resume_context"]))
        return 0;

    return copy(record["resume_context"]);
}

int set_resume_context(string player_id, mapping context)
{
    mapping record;
    mapping previous_context;
    int save_result;

    if (!stringp(player_id) || player_id == "")
        return 0;

    record = ensure_player_record(player_id);
    previous_context = copy(record["resume_context"] || ([]));
    record["resume_context"] = mapp(context) ? copy(context) : ([]);
    save_result = save_player_data();
    if (save_result == 0 && mapp(_last_dungeon_save_phase)
        && _last_dungeon_save_phase["outcome"] == "not_written")
        record["resume_context"] = previous_context;
    return save_result;
}

int clear_resume_context(string player_id)
{
    mapping record;
    mapping previous_context;
    int save_result;

    if (!stringp(player_id) || player_id == "")
        return 0;

    record = ensure_player_record(player_id);
    previous_context = copy(record["resume_context"] || ([]));
    record["resume_context"] = ([]);
    save_result = save_player_data();
    if (save_result == 0 && mapp(_last_dungeon_save_phase)
        && _last_dungeon_save_phase["outcome"] == "not_written")
        record["resume_context"] = previous_context;
    return save_result;
}

void create()
{
    ::create();
    restore_player_data();
}
