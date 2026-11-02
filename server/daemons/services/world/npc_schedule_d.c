/**
 * @file    npc_schedule_d.c
 * @brief   NPC 日程移动（世界动态性：NPC 按时辰出现在不同房间）。
 *
 * NPC 模板配置 schedule（时辰→房间路径）+ schedule_default（默认房间），
 * NPC 实例通过 track_npc 注册；daemon 每现实分钟检查时辰变化，
 * 到点时把 NPC 移动到目标房间（复用 WORLD_TRAVEL_D->move_actor）。
 * 战斗中的 NPC 不移动；对话中移动由 move_actor 自身语义保证。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/framework/daemon";

nosave object *_tracked;
/* 兼容旧单文件的全局时辰；新的 checkpoint 按公共 region scope 保存。 */
int _last_hour;
mapping _last_hour_by_scope;
/* restore_object 用来识别 scope shard 的普通保存字段。 */
string _schedule_scope_key;
int _schedule_scope_hour;
int _schedule_scope_world_generation;
int _schedule_scope_deleted;
mapping _schedule_scope_entries;
nosave mapping _persisted_schedule_scopes;
nosave int _tick_handle;
nosave int _schedule_pending;
nosave mapping _pending_scopes;
nosave mapping _synthetic_schedules;
nosave mapping _synthetic_fail_scopes;
nosave string _pending_scope_key;
nosave int _pending_hour;
nosave int _persist_retry_count;
nosave int _persist_retry_handle;
nosave int _dropped_slots;
nosave string _last_persist_result;
nosave string _last_persist_error;
nosave mapping _last_schedule_save_phase;

private void schedule_next_tick();
private void check_hour_tick();
void restore_schedule_state();
int restore_synthetic_scope(string scope_key);
private int persist_schedule_scope(string scope_key, int hour);
private int persist_schedule_tombstone(string scope_key);
private int apply_all_schedules(string scope_key, int hour);
private void retry_pending_schedule();
private void schedule_schedule_retry();
private string query_scope_save_file(string scope_key);
private string query_npc_scope_key(object npc);
private int registered_scope(string scope_key);
private int synthetic_scope_registered(string scope_key);
private void refresh_pending_summary();
private void mark_pending_scope(string scope_key, int hour);

private mapping deep_copy_value(mixed value)
{
    mapping copied;
    mixed err;

    err = catch(copied = restore_variable(save_variable(value)));
    return err || !mapp(copied) ? ([ ]) : copied;
}

private int registered_scope(string scope_key)
{
    mapping region;

    if (!stringp(scope_key) || scope_key == "")
        return 0;
    foreach (region in values(REGION_D->query_all_regions() || ([ ])))
        if (mapp(region) && region["enabled"]
            && region["scope_key"] == scope_key)
            return 1;
    return 0;
}

private int synthetic_scope_registered(string scope_key)
{
    if (!stringp(scope_key) || strsrch(scope_key, "shared:synthetic_") != 0)
        return 0;
    return registered_scope(scope_key);
}

void create()
{
    ::create();
    _tracked = ({ });
    _last_hour_by_scope = ([ ]);
    _schedule_scope_key = "";
    _schedule_scope_hour = -1;
    _schedule_scope_world_generation = 0;
    _schedule_scope_deleted = 0;
    _schedule_scope_entries = ([ ]);
    _persisted_schedule_scopes = ([ ]);
    _schedule_pending = 0;
    _pending_scopes = ([ ]);
    _synthetic_schedules = ([ ]);
    _synthetic_fail_scopes = ([ ]);
    _pending_scope_key = "";
    _pending_hour = -1;
    _persist_retry_count = 0;
    _persist_retry_handle = 0;
    _dropped_slots = 0;
    _last_persist_result = "not_attempted";
    _last_persist_error = "";
    restore_schedule_state();
    /* restart reconciliation is retried until the current-hour placement and
     * durable checkpoint both succeed. */
    catch(check_hour_tick());
}

private string query_save_file()
{
    return DATA_DIR + "runtime/npc_schedule_d";
}

private string query_scope_save_file(string scope_key)
{
    return DATA_DIR + "runtime/npc_schedule_scope_"
        + hash("sha256", scope_key);
}

private string query_npc_scope_key(object npc)
{
    mapping context;
    object room;

    if (!objectp(npc) || !objectp(room = environment(npc)))
        return "";
    context = REGION_D->resolve_region_context(room);
    return mapp(context) && context["ok"] == 1
        && stringp(context["scope_key"]) ? context["scope_key"] : "";
}

/* Public for lifecycle/synthetic diagnostics; schedule identity is a region
 * scope, never an LPC object reference or a guessed room prefix. */
string query_npc_scope(object npc)
{
    return query_npc_scope_key(npc);
}

int synthetic_fail_next_scope_persist(string scope_key)
{
    if (!synthetic_scope_registered(scope_key))
        return 0;
    if (!mapp(_synthetic_fail_scopes))
        _synthetic_fail_scopes = ([ ]);
    _synthetic_fail_scopes[scope_key] = 1;
    return 1;
}

mapping synthetic_retry_pending()
{
    int before;

    before = sizeof(_pending_scopes || ([ ]));
    retry_pending_schedule();
    return ([
        "ok": sizeof(_pending_scopes || ([ ])) == 0,
        "pending": sizeof(_pending_scopes || ([ ])) > 0 ? 1 : 0,
        "recovered": before > 0 && sizeof(_pending_scopes || ([ ])) == 0,
        "pending_scope_keys": keys(_pending_scopes || ([ ])),
    ]);
}

mapping synthetic_checkpoint_scope_schedule(string scope_key,
    string npc_id, string schedule_id, int hour)
{
    mapping entries;
    mapping previous_entries;
    mapping entry;
    string identity;

    if (!registered_scope(scope_key) || !stringp(npc_id) || npc_id == ""
        || !stringp(schedule_id) || schedule_id == ""
        || !intp(hour) || hour < 0 || hour >= TIME_D->query_hours_per_day())
        return ([ "ok": 0, "error": "schedule_args_invalid" ]);
    previous_entries = mapp(_synthetic_schedules[scope_key])
        ? deep_copy_value(_synthetic_schedules[scope_key]) : ([ ]);
    entries = mapp(_synthetic_schedules[scope_key])
        ? _synthetic_schedules[scope_key] : ([ ]);
    identity = npc_id + "/" + schedule_id;
    entry = entries[identity];
    if (mapp(entry) && entry["hour"] == hour)
        return ([ "ok": 1, "duplicate": 1, "state": "checkpointed",
            "scope_key": scope_key, "identity": identity,
            "hour": hour ]);
    if (mapp(entry) && intp(entry["hour"]) && entry["hour"] > hour)
        return ([ "ok": 0, "error": "stale_schedule_checkpoint",
            "scope_key": scope_key, "identity": identity,
            "current_hour": entry["hour"], "hour": hour ]);
    entries[identity] = ([
        "npc_id": npc_id,
        "schedule_id": schedule_id,
        "identity": identity,
        "scope_key": scope_key,
        "hour": hour,
        "generation": mapp(entry) && intp(entry["generation"])
            ? entry["generation"] + 1 : 1,
        "world_generation": WORLD_STATE_D->query_world_generation(),
        "checkpointed_at": time(),
    ]);
    _synthetic_schedules[scope_key] = entries;
    if (!persist_schedule_scope(scope_key, hour)) {
        /* Keep the candidate in memory and mark the scope pending. The
         * retry path can therefore persist the exact same checkpoint rather
         * than reconstructing a potentially different schedule. */
        if (mapp(_last_schedule_save_phase)
            && _last_schedule_save_phase["outcome"] == "not_written")
            _synthetic_schedules[scope_key] = previous_entries;
        else
            _synthetic_schedules[scope_key] = entries;
        mark_pending_scope(scope_key, hour);
        return ([ "ok": 0, "error": "persistence_failed",
            "recovery_required": 1, "scope_key": scope_key,
            "identity": identity ]);
    }
    _last_hour_by_scope[scope_key] = hour;
    _persisted_schedule_scopes[scope_key] = 1;
    return ([ "ok": 1, "duplicate": 0, "state": "checkpointed",
        "scope_key": scope_key, "identity": identity, "hour": hour,
        "generation": entries[identity]["generation"] ]);
}

mapping query_synthetic_scope_schedule_snapshot(string scope_key)
{
    mapping entries;
    mapping result;

    if (!registered_scope(scope_key))
        return ([ "ok": 0, "error": "scope_not_registered" ]);
    entries = mapp(_synthetic_schedules[scope_key])
        ? deep_copy_value(_synthetic_schedules[scope_key]) : ([ ]);
    result = ([
        "ok": 1,
        "scope_key": scope_key,
        "entries": entries,
        "entry_count": sizeof(entries),
        "last_hour": intp(_last_hour_by_scope[scope_key])
            ? _last_hour_by_scope[scope_key] : -1,
        "world_generation": WORLD_STATE_D->query_world_generation(),
        "pending": mapp(_pending_scopes[scope_key]) ? 1 : 0,
    ]);
    return result;
}

int clear_synthetic_scope_schedule(string scope_key)
{
    mapping entry;
    string file;
    string identity;
    mixed err;

    if (!stringp(scope_key) || strsrch(scope_key, "shared:synthetic_") != 0)
        return 0;
    foreach (identity, entry in _synthetic_schedules[scope_key] || ([ ]))
        if (!mapp(entry) || entry["scope_key"] != scope_key)
            return 0;
    map_delete(_synthetic_schedules, scope_key);
    map_delete(_last_hour_by_scope, scope_key);
    map_delete(_pending_scopes, scope_key);
    map_delete(_persisted_schedule_scopes, scope_key);
    refresh_pending_summary();
    if (!_schedule_pending && _persist_retry_handle) {
        remove_call_out(_persist_retry_handle);
        _persist_retry_handle = 0;
    }
    map_delete(_synthetic_fail_scopes, scope_key);
    file = query_scope_save_file(scope_key) + ".o";
    err = file_size(file) > 0 ? catch(rm(file)) : 0;
    return !err;
}

int purge_synthetic_scope_schedule(string scope_key)
{
    mapping previous_entries;
    mixed previous_hour;

    if (!registered_scope(scope_key))
        return 0;
    previous_entries = mapp(_synthetic_schedules[scope_key])
        ? deep_copy_value(_synthetic_schedules[scope_key]) : ([ ]);
    previous_hour = _last_hour_by_scope[scope_key];
    map_delete(_synthetic_schedules, scope_key);
    if (persist_schedule_tombstone(scope_key) != 1) {
        if (mapp(_last_schedule_save_phase)
            && _last_schedule_save_phase["outcome"] == "not_written") {
            if (sizeof(previous_entries) > 0)
                _synthetic_schedules[scope_key] = previous_entries;
            if (intp(previous_hour))
                _last_hour_by_scope[scope_key] = previous_hour;
        }
        mark_pending_scope(scope_key, -1);
        return 0;
    }
    map_delete(_last_hour_by_scope, scope_key);
    map_delete(_persisted_schedule_scopes, scope_key);
    return 1;
}

void restore_schedule_state()
{
    string file;
    string runtime_dir;
    string entry_name;
    string suffix;
    string scope_key;
    string legacy_scope;
    string *entries;
    mapping legacy_hours;
    mapping restored_hours;
    mapping restored_schedules;
    mixed err;
    int legacy_hour;
    int shard_hour;

    file = query_save_file();
    _last_hour = -1;
    _last_hour_by_scope = ([ ]);
    _synthetic_schedules = ([ ]);
    _schedule_scope_entries = ([ ]);
    _persisted_schedule_scopes = ([ ]);
    legacy_hours = ([ ]);
    restored_hours = ([ ]);
    restored_schedules = ([ ]);
    legacy_hour = -1;
    if (file_size(file + ".o") > 0) {
        err = catch(restore_object(file));
        if (err) {
            _last_persist_result = "restore_failed";
            _last_persist_error = sprintf("%O", err);
        } else {
            legacy_hour = intp(_last_hour) ? _last_hour : -1;
            if (mapp(_last_hour_by_scope))
                legacy_hours = copy(_last_hour_by_scope);
        }
    }
    foreach (legacy_scope, shard_hour in legacy_hours) {
        if (!stringp(legacy_scope) || legacy_scope == ""
            || !intp(shard_hour))
            continue;
        restored_hours[legacy_scope] = shard_hour;
        _persisted_schedule_scopes[legacy_scope] = 1;
    }
    _last_hour = legacy_hour;

    runtime_dir = DATA_DIR + "runtime";
    entries = get_dir(runtime_dir + "/");
    if (arrayp(entries))
        foreach (entry_name in entries) {
            if (!stringp(entry_name)
                || sscanf(entry_name, "npc_schedule_scope_%s.o", suffix) != 1
                || file_size(runtime_dir + "/" + entry_name) <= 0)
                continue;
            _schedule_scope_key = "";
            _schedule_scope_hour = -1;
            _schedule_scope_world_generation = 0;
            _schedule_scope_deleted = 0;
            _schedule_scope_entries = ([ ]);
            _last_hour_by_scope = ([ ]);
            err = catch(restore_object(runtime_dir + "/" + entry_name));
            if (err || !stringp(_schedule_scope_key)
                || _schedule_scope_key == "")
                continue;
            scope_key = _schedule_scope_key;
            if (!_schedule_scope_deleted && mapp(_schedule_scope_entries))
                restored_schedules[scope_key] = deep_copy_value(
                    _schedule_scope_entries);
            shard_hour = intp(_schedule_scope_hour)
                ? _schedule_scope_hour : -1;
            if (!_schedule_scope_deleted)
                _persisted_schedule_scopes[scope_key] = 1;
            if (!_schedule_scope_deleted && shard_hour >= 0)
                restored_hours[scope_key] = shard_hour;
            else {
                map_delete(restored_hours, scope_key);
                map_delete(restored_schedules, scope_key);
                map_delete(_persisted_schedule_scopes, scope_key);
            }
            if (shard_hour > _last_hour)
                _last_hour = shard_hour;
        }
    _last_hour_by_scope = restored_hours;
    _synthetic_schedules = restored_schedules;
    _schedule_scope_key = "";
    _schedule_scope_hour = -1;
    _schedule_scope_world_generation = 0;
    _schedule_scope_deleted = 0;
    _schedule_scope_entries = ([ ]);
}

/* Reload one synthetic schedule shard without resetting live NPC schedules or
 * unrelated pending checkpoints. */
int restore_synthetic_scope(string scope_key)
{
    mapping previous_hours;
    mapping previous_schedules;
    mapping shard_entries;
    string previous_scope;
    string file;
    string shard_scope;
    mixed err;
    int previous_hour;
    int previous_scope_hour;
    int previous_scope_generation;
    int previous_scope_deleted;
    int shard_hour;
    int shard_deleted;

    if (!synthetic_scope_registered(scope_key))
        return 0;
    file = query_scope_save_file(scope_key) + ".o";
    if (file_size(file) <= 0)
        return 0;
    previous_hours = _last_hour_by_scope;
    previous_schedules = _synthetic_schedules;
    previous_hour = _last_hour;
    previous_scope = _schedule_scope_key;
    previous_scope_hour = _schedule_scope_hour;
    previous_scope_generation = _schedule_scope_world_generation;
    previous_scope_deleted = _schedule_scope_deleted;
    _last_hour_by_scope = ([ ]);
    _schedule_scope_key = "";
    _schedule_scope_hour = -1;
    _schedule_scope_world_generation = 0;
    _schedule_scope_deleted = 0;
    _schedule_scope_entries = ([ ]);
    err = catch(restore_object(file));
    shard_scope = _schedule_scope_key;
    shard_hour = _schedule_scope_hour;
    shard_deleted = _schedule_scope_deleted;
    shard_entries = !err && mapp(_schedule_scope_entries)
        ? deep_copy_value(_schedule_scope_entries) : 0;
    _last_hour_by_scope = previous_hours;
    _synthetic_schedules = previous_schedules;
    _last_hour = previous_hour;
    _schedule_scope_key = previous_scope;
    _schedule_scope_hour = previous_scope_hour;
    _schedule_scope_world_generation = previous_scope_generation;
    _schedule_scope_deleted = previous_scope_deleted;
    _schedule_scope_entries = ([ ]);
    if (err || shard_scope != scope_key || !mapp(shard_entries))
        return 0;
    if (shard_deleted) {
        map_delete(_synthetic_schedules, scope_key);
        map_delete(_last_hour_by_scope, scope_key);
        map_delete(_persisted_schedule_scopes, scope_key);
    } else {
        if (shard_hour < 0)
            return 0;
        _synthetic_schedules[scope_key] = shard_entries;
        _last_hour_by_scope[scope_key] = shard_hour;
        _persisted_schedule_scopes[scope_key] = 1;
        if (shard_hour > _last_hour)
            _last_hour = shard_hour;
    }
    return 1;
}

private int persist_schedule_tombstone(string scope_key)
{
    mapping previous_hours;
    string previous_scope;
    int previous_hour;
    int previous_world_generation;
    int previous_deleted;
    string file;
    string runtime_dir;
    mixed err;
    mapping detail;

    if (!stringp(scope_key) || scope_key == "")
        return 0;
    runtime_dir = DATA_DIR + "runtime";
    if (file_size(runtime_dir) == -1 && !mkdir(runtime_dir))
        return 0;
    previous_hours = _last_hour_by_scope;
    previous_scope = _schedule_scope_key;
    previous_hour = _schedule_scope_hour;
    previous_world_generation = _schedule_scope_world_generation;
    previous_deleted = _schedule_scope_deleted;
    _schedule_scope_entries = ([ ]);
    _last_hour_by_scope = ([ ]);
    _schedule_scope_key = scope_key;
    _schedule_scope_hour = -1;
    _schedule_scope_world_generation = WORLD_STATE_D->query_world_generation();
    _schedule_scope_deleted = 1;
    file = query_scope_save_file(scope_key);
    if (mapp(_synthetic_fail_scopes)
        && _synthetic_fail_scopes[scope_key]) {
        map_delete(_synthetic_fail_scopes, scope_key);
        _last_persist_result = "failed";
        _last_persist_error = "synthetic schedule persistence failure";
        _last_hour_by_scope = previous_hours;
        _schedule_scope_key = previous_scope;
        _schedule_scope_hour = previous_hour;
        _schedule_scope_world_generation = previous_world_generation;
        _schedule_scope_deleted = previous_deleted;
        _schedule_scope_entries = ([ ]);
        return 0;
    }
    err = catch(detail = owner_save_object_phases(file,
        "owner/region_scope/service/world/npc_schedule/" + scope_key,
        "npc_schedule_scope", ([
            "scope_key": scope_key,
            "last_hour": -1,
            "world_generation": _schedule_scope_world_generation,
            "deleted": 1,
            "schedule_count": 0,
            "state_digest": hash("sha256", save_variable(_schedule_scope_entries)),
        ]), "npc_schedule/tombstone/" + hash("sha256",
            scope_key + "/-1/" + sprintf("%d", _schedule_scope_world_generation)
            + "/" + save_variable(_schedule_scope_entries))));
    _last_hour_by_scope = previous_hours;
    _schedule_scope_key = previous_scope;
    _schedule_scope_hour = previous_hour;
    _schedule_scope_world_generation = previous_world_generation;
    _schedule_scope_deleted = previous_deleted;
    _schedule_scope_entries = ([ ]);
    _last_schedule_save_phase = err || !mapp(detail)
        ? ([ "outcome": "unknown", "persisted": -1, "save_result": 0,
            "error": err ? sprintf("%O", err) : "schedule save unavailable" ])
        : copy(detail);
    if (!mapp(detail) || detail["persisted"] != 1)
        return 0;
    return detail["ok"] == 1 ? 1 : 2;
}

private int persist_schedule_scope(string scope_key, int hour)
{
    mapping previous_hours;
    string previous_scope;
    int previous_hour;
    int previous_world_generation;
    int previous_deleted;
    string file;
    string runtime_dir;
    mixed err;
    mapping detail;

    if (!stringp(scope_key) || scope_key == "" || hour < 0)
        return 0;
    runtime_dir = DATA_DIR + "runtime";
    if (file_size(runtime_dir) == -1 && !mkdir(runtime_dir)) {
        _last_persist_result = "failed";
        _last_persist_error = "runtime directory creation failed";
        return 0;
    }
    previous_hours = _last_hour_by_scope;
    previous_scope = _schedule_scope_key;
    previous_hour = _schedule_scope_hour;
    previous_world_generation = _schedule_scope_world_generation;
    previous_deleted = _schedule_scope_deleted;
    _schedule_scope_entries = mapp(_synthetic_schedules[scope_key])
        ? deep_copy_value(_synthetic_schedules[scope_key]) : ([ ]);
    _last_hour_by_scope = ([ scope_key: hour ]);
    _schedule_scope_key = scope_key;
    _schedule_scope_hour = hour;
    _schedule_scope_world_generation = WORLD_STATE_D->query_world_generation();
    _schedule_scope_deleted = 0;
    file = query_scope_save_file(scope_key);
    if (mapp(_synthetic_fail_scopes)
        && _synthetic_fail_scopes[scope_key]) {
        map_delete(_synthetic_fail_scopes, scope_key);
        _last_persist_result = "failed";
        _last_persist_error = "synthetic schedule persistence failure";
        _last_hour_by_scope = previous_hours;
        _schedule_scope_key = previous_scope;
        _schedule_scope_hour = previous_hour;
        _schedule_scope_world_generation = previous_world_generation;
        _schedule_scope_deleted = previous_deleted;
        _schedule_scope_entries = ([ ]);
        return 0;
    }
    err = catch(detail = owner_save_object_phases(file,
        "owner/region_scope/service/world/npc_schedule/" + scope_key,
        "npc_schedule_scope", ([
            "scope_key": scope_key,
            "last_hour": hour,
            "world_generation": _schedule_scope_world_generation,
            "deleted": 0,
            "schedule_count": sizeof(_schedule_scope_entries || ([])),
            "state_digest": hash("sha256", save_variable(_schedule_scope_entries)),
        ]), "npc_schedule/scope/" + hash("sha256",
            scope_key + "/" + sprintf("%d", hour) + "/"
            + sprintf("%d", _schedule_scope_world_generation) + "/"
            + save_variable(_schedule_scope_entries))));
    _last_hour_by_scope = previous_hours;
    _schedule_scope_key = previous_scope;
    _schedule_scope_hour = previous_hour;
    _schedule_scope_world_generation = previous_world_generation;
    _schedule_scope_deleted = previous_deleted;
    _schedule_scope_entries = ([ ]);
    if (err || !mapp(detail)) {
        _last_persist_result = "unknown";
        _last_persist_error = err ? sprintf("%O", err) : "schedule save unavailable";
        _last_schedule_save_phase = ([ "outcome": "unknown", "persisted": -1,
            "save_result": 0, "error": _last_persist_error ]);
        return 0;
    }
    _last_schedule_save_phase = copy(detail);
    _last_persist_result = detail["outcome"] || "unknown";
    _last_persist_error = detail["error"] || "";
    return detail["persisted"] == 1 && detail["ok"] == 1 ? 1
        : detail["persisted"] == 1 ? 2 : 0;
}

int track_npc(object npc)
{
    if (!objectp(npc) || query_npc_scope_key(npc) == "")
        return 0;
    if (member_array(npc, _tracked) == -1) {
        _tracked += ({ npc });
    }
    return 1;
}

/* spawn 生命周期注册点：仅模板带 schedule/schedule_default 的 NPC 登记，
 * 壳保持声明式（create 不直接 track）。 */
int maybe_track_npc(object npc)
{
    if (!objectp(npc))
        return 0;
    if (!mapp(npc->query("schedule"))
        && !stringp(npc->query("schedule_default")))
        return 0;
    return track_npc(npc);
}

int untrack_npc(object npc)
{
    int idx;

    if (!objectp(npc))
        return 0;
    idx = member_array(npc, _tracked);
    if (idx != -1)
        _tracked -= ({ npc });
    return idx != -1;
}

/* 供 NPC 壳在 destruct 时调用（cleanup 钩子） */
void on_npc_destructed(object npc)
{
    untrack_npc(npc);
}

/* 模板级日程查询（供 ensure 补刷判定）：返回模板当前时辰的期望房间（""=无日程） */
string query_template_target_room(string template_id)
{
    mapping template;
    mixed schedule;
    string default_room;
    int hour;

    if (!stringp(template_id) || template_id == "")
        return "";
    template = CONTENT_REGISTRY_D->query_npc_template(template_id);
    if (!mapp(template))
        return "";
    schedule = template["schedule"];
    default_room = template["schedule_default"] || "";
    if (!mapp(schedule) && default_room == "")
        return "";
    hour = TIME_D->query_hour();
    if (mapp(schedule) && stringp(schedule[hour]) && schedule[hour] != "")
        return schedule[hour];
    return default_room;
}

/* ensure 补刷判定：模板有日程且期望房间不是本房间时，跳过本房间补刷 */
int should_skip_room(object room, mapping entity)
{
    string template_id;
    string target_room;
    string room_path;

    if (!objectp(room) || !mapp(entity))
        return 0;
    template_id = entity["template_id"] || "";
    if (template_id == "")
        return 0;
    target_room = query_template_target_room(template_id);
    if (target_room == "")
        return 0;
    room_path = base_name(room);
    return room_path != target_room;
}

/* 地图标记在场判定（与 should_skip_room 同源 query_template_target_room，
 * 判定粒度不同）：provider 当前时辰是否应出现在该房间（按地图格子）。
 * 模板无日程视为常驻；室内日程目标（含 __ 子房）按根房投影，与地图格子
 * 把室内实体投影到根房标记的语义一致（投影规则唯一权威：
 * CONTENT_REGISTRY_D->query_indoor_root_room_path）；根房目标不向子房
 * 查询投影，与 ensure 精确匹配一致。
 * 注意：spawn 条目必须携带 template_id（仅 provider_id 的条目在
 * room_provider_templates 查不到模板，此处按常驻处理，日程过滤会静默失效）。 */
int query_provider_present_in_room(string provider_id, string room_path)
{
    string *templates;
    string target_room;
    string target_root;
    string room_root;

    if (!stringp(provider_id) || provider_id == "" || !stringp(room_path)
        || room_path == "")
        return 0;
    templates = CONTENT_REGISTRY_D->query_room_provider_templates(
        room_path, provider_id);
    if (sizeof(templates) == 0)
        return 1; /* 无模板信息（与 ensure 一致视为常驻） */
    foreach (string template_id in templates) {
        target_room = query_template_target_room(template_id);
        if (target_room == "" || target_room == room_path)
            return 1;
        if (strsrch(target_room, "__") != -1) {
            /* 室内目标投影：根房格或同楼子房查询均显示标记 */
            target_root = CONTENT_REGISTRY_D->query_indoor_root_room_path(target_room);
            room_root = strsrch(room_path, "__") != -1
                ? CONTENT_REGISTRY_D->query_indoor_root_room_path(room_path)
                : room_path;
            if (target_root == room_root)
                return 1;
        }
    }
    return 0;
}

private string query_target_room(object npc, int hour)
{
    mixed schedule;
    string default_room;

    if (!objectp(npc))
        return "";
    schedule = npc->query("schedule");
    default_room = npc->query("schedule_default") || "";
    if (mapp(schedule) && stringp(schedule[hour]) && schedule[hour] != "")
        return schedule[hour];
    return default_room;
}

private int apply_schedule_for_npc(object npc, string scope_key, int hour)
{
    object env;
    object new_env;
    mapping moved;
    mapping current_context;
    mapping target_context;
    string target_room;
    string current_room;

    if (!objectp(npc) || !objectp(environment(npc)))
        return 1;
    if (query_npc_scope_key(npc) != scope_key)
        return 1;
    /* 战斗中不移动；下一个时辰重新检查。 */
    if (npc->query_temp("combat/in_fight") || npc->query("combat/in_fight"))
        return 1;

    target_room = query_target_room(npc, hour);
    if (target_room == "")
        return 1;

    env = environment(npc);
    current_room = base_name(env);
    current_context = REGION_D->resolve_region_context(env);
    target_context = REGION_D->resolve_region_context(target_room);
    if (!mapp(current_context) || current_context["ok"] != 1
        || !mapp(target_context) || target_context["ok"] != 1
        || current_context["scope_key"] != scope_key
        || target_context["scope_key"] != scope_key)
        return 0;
    if (current_room == target_room)
        return 1;

    moved = WORLD_TRAVEL_D->move_actor(npc, target_room, ([
        "reason": "npc_schedule",
        "quiet": 1,
        "expected_location_generation":
            npc->query_temp("world/location_generation") || 0,
    ]));
    if (!mapp(moved) || !moved["ok"])
        return 0;
    /* 移动成功后重索引 content-entity spawn slot：旧房 key 摘除、新房登记，
     * 否则时辰边界窗口内新房 ensure 索引 miss 会补刷克隆（双生）。 */
    new_env = environment(npc);
    if (objectp(new_env) && new_env != env
        && npc->query_temp("world_lifecycle/content_entity_id") != ""
        && !WORLD_ENTITY_LIFECYCLE_D->reindex_content_entity(npc, env, new_env)) {
        log_file("system/world", sprintf("[%s] schedule reindex rejected for %s\n",
            ctime(time())[4..19], file_name(npc)));
        return 0;
    }
    return objectp(new_env) && new_env != env;
}

private int apply_all_schedules(string scope_key, int hour)
{
    object *snapshot;
    object *live_tracked;
    int all_applied;
    int i;

    if (!stringp(scope_key) || scope_key == "")
        return 0;
    snapshot = _tracked + ({ });
    live_tracked = ({ });
    all_applied = 1;
    for (i = 0; i < sizeof(snapshot); i++) {
        if (!objectp(snapshot[i])) {
            _dropped_slots++;
            continue;
        }
        if (query_npc_scope_key(snapshot[i]) == scope_key) {
            live_tracked += ({ snapshot[i] });
            if (!apply_schedule_for_npc(snapshot[i], scope_key, hour))
                all_applied = 0;
        } else if (query_npc_scope_key(snapshot[i]) != "") {
            live_tracked += ({ snapshot[i] });
        }
    }
    _tracked = live_tracked;
    return all_applied;
}

private string *query_active_scope_keys()
{
    mapping seen;
    object npc;
    string scope_key;

    seen = ([ ]);
    foreach (npc in _tracked || ({ })) {
        scope_key = query_npc_scope_key(npc);
        if (scope_key != "")
            seen[scope_key] = 1;
    }
    foreach (scope_key in keys(_persisted_schedule_scopes || ([ ])))
        if (stringp(scope_key) && scope_key != "")
            seen[scope_key] = 1;
    foreach (scope_key in keys(_last_hour_by_scope || ([ ])))
        if (stringp(scope_key) && scope_key != "")
            seen[scope_key] = 1;
    return keys(seen);
}

private void refresh_pending_summary()
{
    string *scope_keys;
    string first_scope;

    scope_keys = keys(_pending_scopes || ([ ]));
    _schedule_pending = sizeof(scope_keys) > 0;
    first_scope = sizeof(scope_keys) > 0 ? scope_keys[0] : "";
    _pending_scope_key = first_scope;
    _pending_hour = mapp(_pending_scopes[first_scope])
        && intp(_pending_scopes[first_scope]["hour"])
            ? _pending_scopes[first_scope]["hour"] : -1;
    _persist_retry_count = mapp(_pending_scopes[first_scope])
        && intp(_pending_scopes[first_scope]["retry_count"])
            ? _pending_scopes[first_scope]["retry_count"] : 0;
}

private void mark_pending_scope(string scope_key, int hour)
{
    if (!stringp(scope_key) || scope_key == "")
        return;
    if (!mapp(_pending_scopes))
        _pending_scopes = ([ ]);
    if (!mapp(_pending_scopes[scope_key]))
        _pending_scopes[scope_key] = ([
            "hour": hour,
            "retry_count": 0,
        ]);
    else
        _pending_scopes[scope_key]["hour"] = hour;
    refresh_pending_summary();
    schedule_schedule_retry();
}

private void schedule_schedule_retry()
{
    mapping pending;
    int retryable;

    if (_persist_retry_handle || !_schedule_pending)
        return;
    foreach (pending in values(_pending_scopes || ([ ]))) {
        if (mapp(pending) && (!intp(pending["retry_count"])
            || pending["retry_count"] < 3)) {
            retryable = 1;
            break;
        }
    }
    if (retryable)
        _persist_retry_handle = call_out("retry_pending_schedule", 5);
}

private void retry_pending_schedule()
{
    mapping pending;
    mapping *failed;
    string scope_key;
    string *scope_keys;
    int hour;
    int previous_hour;
    int applied;
    int saved;
    int retry_count;

    _persist_retry_handle = 0;
    scope_keys = keys(_pending_scopes || ([ ]));
    failed = ({ });
    foreach (scope_key in scope_keys) {
        pending = _pending_scopes[scope_key];
        if (!mapp(pending) || !intp(pending["hour"])) {
            map_delete(_pending_scopes, scope_key);
            continue;
        }
        hour = pending["hour"];
        retry_count = intp(pending["retry_count"])
            ? pending["retry_count"] + 1 : 1;
        previous_hour = intp(_last_hour_by_scope[scope_key])
            ? _last_hour_by_scope[scope_key] : -1;
        if (hour < 0) {
            saved = persist_schedule_tombstone(scope_key);
            if (saved == 1) {
                map_delete(_synthetic_schedules, scope_key);
                map_delete(_last_hour_by_scope, scope_key);
                map_delete(_persisted_schedule_scopes, scope_key);
                map_delete(_pending_scopes, scope_key);
                continue;
            }
        } else {
            applied = apply_all_schedules(scope_key, hour);
            if (applied) {
                _last_hour_by_scope[scope_key] = hour;
                saved = persist_schedule_scope(scope_key, hour);
            }
        }
        if (hour >= 0 && applied) {
            if (saved == 1) {
                _persisted_schedule_scopes[scope_key] = 1;
                map_delete(_pending_scopes, scope_key);
                continue;
            }
            if (!mapp(_last_schedule_save_phase)
                || _last_schedule_save_phase["outcome"] == "not_written") {
                if (previous_hour >= 0)
                    _last_hour_by_scope[scope_key] = previous_hour;
                else
                    map_delete(_last_hour_by_scope, scope_key);
            }
        }
        if (retry_count < 3)
            failed += ({ ([ "scope_key": scope_key,
                "hour": hour, "retry_count": retry_count ]) });
    }
    foreach (pending in failed)
        _pending_scopes[pending["scope_key"]] = pending;
    refresh_pending_summary();
    if (_schedule_pending)
        schedule_schedule_retry();
}

private void check_hour_tick()
{
    string scope_key;
    string *scope_keys;
    int hour;
    int previous_hour;
    int applied;
    int saved;

    _tick_handle = 0;
    hour = TIME_D->query_hour();
    /* Retired configured regions get a tombstone, so an old schedule shard
     * cannot resurrect after a region is removed from the canonical catalog. */
    {
        mapping regions;
        mapping region;
        string retired_scope;
        string *retired_scopes;

        regions = REGION_D->query_all_regions();
        retired_scopes = ({ });
        foreach (retired_scope in keys(_persisted_schedule_scopes || ([ ]))) {
            int live_region;

            live_region = 0;
            foreach (region in values(regions || ([ ])))
                if (mapp(region) && region["scope_key"] == retired_scope)
                    live_region = 1;
            if (!live_region)
                retired_scopes += ({ retired_scope });
        }
        foreach (retired_scope in retired_scopes)
            if (persist_schedule_tombstone(retired_scope)) {
                map_delete(_persisted_schedule_scopes, retired_scope);
                map_delete(_last_hour_by_scope, retired_scope);
            }
    }
    /* Each scope is processed independently. A failed shard is recorded and
     * does not prevent a healthy region from applying and checkpointing. */
    scope_keys = query_active_scope_keys();
    foreach (scope_key in scope_keys) {
        if (intp(_last_hour_by_scope[scope_key])
            && _last_hour_by_scope[scope_key] == hour)
            continue;
        previous_hour = intp(_last_hour_by_scope[scope_key])
            ? _last_hour_by_scope[scope_key] : -1;
        applied = apply_all_schedules(scope_key, hour);
        if (!applied) {
            mark_pending_scope(scope_key, hour);
            continue;
        }
        _last_hour_by_scope[scope_key] = hour;
        saved = persist_schedule_scope(scope_key, hour);
        if (saved != 1) {
            if (!mapp(_last_schedule_save_phase)
                || _last_schedule_save_phase["outcome"] == "not_written") {
                if (previous_hour >= 0)
                    _last_hour_by_scope[scope_key] = previous_hour;
                else
                    map_delete(_last_hour_by_scope, scope_key);
            }
            mark_pending_scope(scope_key, hour);
            continue;
        }
        _persisted_schedule_scopes[scope_key] = 1;
        _last_hour = hour;
        if (mapp(_pending_scopes))
            map_delete(_pending_scopes, scope_key);
    }
    refresh_pending_summary();
    if (_schedule_pending)
        schedule_schedule_retry();
    schedule_next_tick();
}

mapping query_runtime_status()
{
    mapping pending;
    string *scope_keys;
    int max_retry;

    scope_keys = query_active_scope_keys();
    max_retry = 0;
    foreach (pending in values(_pending_scopes || ([ ])))
        if (mapp(pending) && intp(pending["retry_count"])
            && pending["retry_count"] > max_retry)
            max_retry = pending["retry_count"];
    return ([
        "last_hour": _last_hour,
        "last_hour_by_scope": copy(_last_hour_by_scope || ([ ])),
        "scope_keys": scope_keys,
        "scope_count": sizeof(scope_keys),
        "world_generation": WORLD_STATE_D->query_world_generation(),
        "scope_persistence_isolated": 1,
        "schedule_pending": _schedule_pending,
        "pending_scope_key": _pending_scope_key,
        "pending_scope_count": sizeof(_pending_scopes || ([ ])),
        "pending_scope_keys": keys(_pending_scopes || ([ ])),
        "pending_hour": _pending_hour,
        "retry_count": max_retry,
        "last_persist_result": _last_persist_result,
        "last_persist_error": _last_persist_error,
        "tracked_count": sizeof(_tracked || ({ })),
        "tracked_stable_slots": sizeof(_tracked || ({ })),
        "dropped_slots": _dropped_slots,
    ]);
}

private void schedule_next_tick()
{
    if (_tick_handle)
        remove_call_out(_tick_handle);
    _tick_handle = call_out("check_hour_tick", 30);
}
