#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/framework/daemon";

#define EVENT_TICK_SECONDS 30
#define EVENT_RETRY_LIMIT 3

nosave mapping _events;
nosave mapping _event_tombstones;
mapping _active;
/* Event facts are persisted per public region scope. These fields are the
 * temporary envelope used while writing/reading one scope shard; they are
 * ordinary save variables so restore_object can identify a shard. */
string _event_scope_key;
int _event_scope_tick_epoch;
int _event_scope_world_generation;
int _event_scope_deleted;
private int _events_loaded;
mapping _event_scope_tombstones;
nosave mapping _persisted_event_scopes;
nosave int _tick_handle;
/* 最近一次成功持久化的 tick；重启后用于有限 catch-up。 */
int _last_tick_epoch;
nosave int _persist_pending;
nosave int _pending_tick_epoch;
nosave string _pending_scope_key;
nosave string _pending_operation_id;
nosave mapping _pending_active;
nosave mapping _pending_tombstones;
nosave mapping _pending_change;
nosave int _persist_retry_count;
nosave int _persist_retry_handle;
nosave string _last_persist_result;
nosave string _last_persist_error;
nosave mapping _last_event_save_phase;
nosave string _last_catch_up_result;
nosave int _catch_up_count;
nosave int _operation_seq;
nosave string _last_validation_result;
nosave string _last_validation_error;
nosave mapping _synthetic_fail_scopes;
nosave int _event_generation;
nosave int _previous_event_generation;

private void schedule_next_tick();
void restore_event_state();
void on_game_time_hour_changed(string event_name, mixed data);
int restore_synthetic_scope(string scope_key);
private void load_events();
void on_config_reloaded(string key, string config_name);
private void retry_pending_event();
private int reconcile_events(int hour, int tick_epoch, int catch_up);
private int persist_current(string operation_id, mapping change);
private int persist_candidate(mapping candidate, int tick_epoch,
    string operation_id, mapping change);
private mapping deep_copy_value(mixed value);
private string active_key(string event_id, string room_path);
private string query_event_scope_key(mapping event, string room_path);
private string query_active_entry_scope(mapping entry);
private string query_scope_save_file(string scope_key);
private int persist_event_scope(string scope_key, string operation_id);
private object resolve_event_room(string room_path);
private mapping query_active_entry(string event_id, string room_path);
private void schedule_event_retry();
private void ensure_all_active_objects();
private string synthetic_event_key(string scope_key, string event_id);
private int registered_scope(string scope_key);
private int synthetic_scope_valid(string scope_key);

private mapping deep_copy_value(mixed value)
{
    mapping copied;
    mixed err;

    err = catch(copied = restore_variable(save_variable(value)));
    return err ? ([ ]) : copied;
}

private string query_save_file()
{
    return DATA_DIR + "runtime/world_event_d";
}

private string next_operation_id(string prefix)
{
    _operation_seq++;
    return sprintf("world-event/%s/%d/%d/%d", prefix,
        WORLD_STATE_D->query_world_generation(), time(), _operation_seq);
}

private string active_key(string event_id, string room_path)
{
    return event_id + "@" + room_path;
}

private string synthetic_event_key(string scope_key, string event_id)
{
    return "synthetic@" + scope_key + "@" + event_id;
}

private int synthetic_scope_valid(string scope_key)
{
    return stringp(scope_key)
        && strsrch(scope_key, "shared:synthetic_") == 0
        && registered_scope(scope_key);
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

private string query_event_scope_key(mapping event, string room_path)
{
    mapping context;
    mapping region_scope;
    string region_id;
    string scope_key;

    if (!mapp(event) || !stringp(room_path) || room_path == "")
        return "";
    scope_key = event["scope_key"];
    if (stringp(scope_key) && scope_key != "")
        return scope_key;
    region_id = event["region_id"];
    if (stringp(region_id) && region_id != "") {
        region_scope = REGION_D->query_region_scope(region_id);
        if (mapp(region_scope) && stringp(region_scope["scope_key"]))
            return region_scope["scope_key"];
        return "";
    }
    context = REGION_D->resolve_region_context(room_path);
    return mapp(context) && context["ok"] == 1
        && stringp(context["scope_key"]) ? context["scope_key"] : "";
}

private string query_active_entry_scope(mapping entry)
{
    mapping event;

    if (!mapp(entry))
        return "";
    if (stringp(entry["scope_key"]) && entry["scope_key"] != "")
        return entry["scope_key"];
    event = mapp(_events) ? _events[entry["event_id"]] : 0;
    return query_event_scope_key(event, entry["room_path"] || "");
}

private string query_scope_save_file(string scope_key)
{
    return DATA_DIR + "runtime/world_event_scope_" + hash("sha256", scope_key);
}

private int persist_event_scope(string scope_key, string operation_id)
{
    mapping previous_active;
    string previous_scope_key;
    int previous_tick;
    int previous_world_generation;
    int previous_deleted;
    mapping scoped_active;
    mapping entry;
    string key;
    string file;
    string runtime_dir;
    mixed err;
    mapping detail;

    if (!stringp(scope_key) || scope_key == "") {
        _last_event_save_phase = ([
            "outcome": "not_written", "persisted": 0,
            "save_result": 0, "error": "scope key is invalid",
        ]);
        return 0;
    }
    scoped_active = ([]);
    foreach (key, entry in _active || ([ ]))
        if (query_active_entry_scope(entry) == scope_key)
            scoped_active[key] = deep_copy_value(entry);
    previous_active = _active;
    previous_scope_key = _event_scope_key;
    previous_tick = _event_scope_tick_epoch;
    previous_world_generation = _event_scope_world_generation;
    previous_deleted = _event_scope_deleted;
    _event_scope_tombstones = mapp(_event_tombstones[scope_key])
        ? deep_copy_value(_event_tombstones[scope_key]) : ([ ]);
    _active = scoped_active;
    _event_scope_key = scope_key;
    _event_scope_tick_epoch = _last_tick_epoch;
    _event_scope_world_generation = WORLD_STATE_D->query_world_generation();
    _event_scope_deleted = sizeof(scoped_active) == 0;
    runtime_dir = DATA_DIR + "runtime";
    if (file_size(runtime_dir) == -1 && !mkdir(runtime_dir)) {
        _active = previous_active;
        _event_scope_key = previous_scope_key;
        _event_scope_tick_epoch = previous_tick;
        _event_scope_world_generation = previous_world_generation;
        _event_scope_deleted = previous_deleted;
        _event_scope_tombstones = ([ ]);
        _last_event_save_phase = ([
            "outcome": "not_written", "persisted": 0,
            "save_result": 0, "error": "runtime directory creation failed",
        ]);
        return 0;
    }
    file = query_scope_save_file(scope_key);
    if (mapp(_synthetic_fail_scopes)
        && _synthetic_fail_scopes[scope_key]) {
        map_delete(_synthetic_fail_scopes, scope_key);
        _last_persist_error = "synthetic event persistence failure";
        _active = previous_active;
        _event_scope_key = previous_scope_key;
        _event_scope_tick_epoch = previous_tick;
        _event_scope_world_generation = previous_world_generation;
        _event_scope_deleted = previous_deleted;
        _event_scope_tombstones = ([ ]);
        _last_event_save_phase = ([
            "outcome": "not_written", "persisted": 0,
            "save_result": 0,
            "error": "synthetic event persistence failure",
        ]);
        return 0;
    }
    err = catch(detail = owner_save_object_phases(file,
        "owner/world/event/" + scope_key, "world_event_scope", ([
            "scope_key": scope_key,
            "active_count": sizeof(scoped_active),
            "tick_epoch": _last_tick_epoch,
            "world_generation": _event_scope_world_generation,
            "operation_id": operation_id,
            "deleted": _event_scope_deleted,
            "tombstone_count": sizeof(_event_scope_tombstones || ([])),
        ]), "world_event/scope/" + operation_id + "/" + hash("sha256", scope_key)));
    _active = previous_active;
    _event_scope_key = previous_scope_key;
    _event_scope_tick_epoch = previous_tick;
    _event_scope_world_generation = previous_world_generation;
    _event_scope_deleted = previous_deleted;
    _event_scope_tombstones = ([ ]);
    _last_event_save_phase = err || !mapp(detail)
        ? ([ "outcome": "unknown", "persisted": -1, "save_result": 0,
            "error": err ? sprintf("%O", err) : "event save unavailable" ])
        : copy(detail);
    if (!mapp(detail) || detail["persisted"] != 1)
        return 0;
    return detail["ok"] == 1 ? 1 : 2;
}

private string *query_active_scope_keys()
{
    mapping seen;
    mapping entry;
    string scope_key;

    seen = ([ ]);
    foreach (entry in values(_active || ([ ]))) {
        scope_key = query_active_entry_scope(entry);
        if (scope_key != "")
            seen[scope_key] = 1;
    }
    return keys(seen);
}

private mapping query_active_entry(string event_id, string room_path)
{
    mapping entry;
    string key;

    key = active_key(event_id, room_path);
    entry = mapp(_active) ? _active[key] : 0;
    return mapp(entry) ? entry : 0;
}

void restore_event_state()
{
    string file;
    string runtime_dir;
    string entry_name;
    string suffix;
    string scope_key;
    string key;
    string *entries;
    mapping legacy_active;
    mapping shard_active;
    mapping entry;
    mixed err;
    int legacy_tick;
    int shard_tick;
    int has_shards;

    file = query_save_file();
    _persist_pending = 0;
    _pending_tick_epoch = 0;
    _pending_scope_key = "";
    _pending_operation_id = "";
    _pending_active = 0;
    _pending_tombstones = 0;
    _pending_change = 0;
    _persist_retry_count = 0;
    _persist_retry_handle = 0;
    _last_persist_result = "not_attempted";
    _last_persist_error = "";
    _last_catch_up_result = "not_run";
    _catch_up_count = 0;
    _persisted_event_scopes = ([ ]);
    _event_scope_key = "";
    _event_scope_tick_epoch = 0;
    _event_scope_world_generation = 0;
    _event_scope_deleted = 0;
    _event_scope_tombstones = ([ ]);
    _event_tombstones = ([ ]);
    _active = ([ ]);
    _last_tick_epoch = 0;

    if (file_size(file + ".o") > 0) {
        err = catch(restore_object(file));
        if (err) {
            _last_persist_result = "restore_failed";
            _last_persist_error = sprintf("%O", err);
        } else {
            legacy_active = mapp(_active) ? deep_copy_value(_active) : ([ ]);
            legacy_tick = intp(_last_tick_epoch) && _last_tick_epoch >= 0
                ? _last_tick_epoch : 0;
        }
    }
    load_events();
    if (!mapp(legacy_active))
        legacy_active = ([ ]);
    _active = legacy_active;
    _last_tick_epoch = legacy_tick;
    foreach (entry in values(legacy_active)) {
        scope_key = query_active_entry_scope(entry);
        if (scope_key != "")
            _persisted_event_scopes[scope_key] = 1;
    }

    runtime_dir = DATA_DIR + "runtime";
    entries = get_dir(runtime_dir + "/");
    if (arrayp(entries))
        foreach (entry_name in entries) {
            if (!stringp(entry_name)
                || sscanf(entry_name, "world_event_scope_%s.o", suffix) != 1
                || file_size(runtime_dir + "/" + entry_name) <= 0)
                continue;
            has_shards = 1;
            _active = ([ ]);
            _event_scope_key = "";
            _event_scope_tick_epoch = 0;
            _event_scope_world_generation = 0;
            _event_scope_deleted = 0;
            _event_scope_tombstones = ([ ]);
            err = catch(restore_object(runtime_dir + "/" + entry_name));
            if (err || !mapp(_active)
                || !stringp(_event_scope_key) || _event_scope_key == "")
                continue;
            scope_key = _event_scope_key;
            if (!mapp(_event_tombstones))
                _event_tombstones = ([ ]);
            if (mapp(_event_scope_tombstones))
                _event_tombstones[scope_key] = deep_copy_value(
                    _event_scope_tombstones);
            shard_active = deep_copy_value(_active);
            if (!mapp(shard_active))
                continue;
            _persisted_event_scopes[scope_key] = 1;
            /* A shard supersedes the same scope from the legacy file,
             * including an empty tombstone shard. */
            foreach (key, entry in legacy_active)
                if (query_active_entry_scope(entry) == scope_key)
                    map_delete(legacy_active, key);
            foreach (key, entry in shard_active)
                if (mapp(entry))
                    legacy_active[key] = entry;
            shard_tick = intp(_event_scope_tick_epoch)
                && _event_scope_tick_epoch >= 0 ? _event_scope_tick_epoch : 0;
            if (shard_tick > _last_tick_epoch)
                _last_tick_epoch = shard_tick;
        }
    _active = mapp(legacy_active) ? legacy_active : ([ ]);
    if (!has_shards)
        _last_tick_epoch = legacy_tick;
    _event_scope_key = "";
    _event_scope_tick_epoch = 0;
    _event_scope_world_generation = 0;
    _event_scope_deleted = 0;
    _event_scope_tombstones = ([ ]);
}

/* Reload only one synthetic scope from its durable shard. The normal daemon
 * state, pending recovery, and unrelated region scopes remain untouched. */
int restore_synthetic_scope(string scope_key)
{
    mapping previous_active;
    mapping shard_active;
    mapping shard_tombstones;
    mapping entry;
    mapping previous_scope_tombstones;
    string previous_scope;
    string file;
    string key;
    string shard_scope;
    mixed err;
    int previous_scope_tick;
    int previous_scope_generation;
    int previous_scope_deleted;
    int previous_last_tick;
    int previous_operation_seq;
    int shard_deleted;

    if (!synthetic_scope_valid(scope_key))
        return 0;
    file = query_scope_save_file(scope_key) + ".o";
    if (file_size(file) <= 0)
        return 0;
    previous_active = _active;
    previous_scope = _event_scope_key;
    previous_scope_tick = _event_scope_tick_epoch;
    previous_scope_generation = _event_scope_world_generation;
    previous_scope_deleted = _event_scope_deleted;
    previous_scope_tombstones = _event_scope_tombstones;
    previous_last_tick = _last_tick_epoch;
    previous_operation_seq = _operation_seq;
    _active = ([ ]);
    _event_scope_key = "";
    _event_scope_tick_epoch = 0;
    _event_scope_world_generation = 0;
    _event_scope_deleted = 0;
    _event_scope_tombstones = ([ ]);
    err = catch(restore_object(file));
    shard_scope = _event_scope_key;
    shard_deleted = _event_scope_deleted;
    shard_active = !err && mapp(_active) ? deep_copy_value(_active) : 0;
    shard_tombstones = !err && mapp(_event_scope_tombstones)
        ? deep_copy_value(_event_scope_tombstones) : ([ ]);
    _active = previous_active;
    _event_scope_key = previous_scope;
    _event_scope_tick_epoch = previous_scope_tick;
    _event_scope_world_generation = previous_scope_generation;
    _event_scope_deleted = previous_scope_deleted;
    _event_scope_tombstones = previous_scope_tombstones;
    _last_tick_epoch = previous_last_tick;
    _operation_seq = previous_operation_seq;
    if (err || shard_scope != scope_key || !mapp(shard_active))
        return 0;
    foreach (key in keys(_active || ([ ])))
        if (mapp(_active[key]) && _active[key]["scope_key"] == scope_key
            && _active[key]["synthetic"] == 1)
            map_delete(_active, key);
    if (!shard_deleted)
        foreach (key, entry in shard_active)
            if (mapp(entry) && entry["scope_key"] == scope_key
                && entry["synthetic"] == 1)
                _active[key] = entry;
    if (!mapp(_event_tombstones))
        _event_tombstones = ([ ]);
    if (sizeof(shard_tombstones) > 0)
        _event_tombstones[scope_key] = shard_tombstones;
    else
        map_delete(_event_tombstones, scope_key);
    if (!mapp(_persisted_event_scopes))
        _persisted_event_scopes = ([ ]);
    if (shard_deleted && sizeof(shard_tombstones) == 0)
        map_delete(_persisted_event_scopes, scope_key);
    else
        _persisted_event_scopes[scope_key] = 1;
    return 1;
}

private int persist_current(string operation_id, mapping change)
{
    mapping event;
    mapping entry;
    string scope_key;
    string event_id;
    string file;
    string *scope_keys;
    mixed err;
    int scope_result;
    int pending_phase;
    int synthetic_change;

    if (!mapp(_active))
        _active = ([ ]);
    scope_keys = ({ });
    synthetic_change = mapp(change) && stringp(change["kind"])
        && strsrch(change["kind"], "synthetic_") == 0;
    if (mapp(change) && stringp(change["scope_key"])
        && change["scope_key"] != "")
        scope_keys += ({ change["scope_key"] });
    /* Synthetic acceptance is explicitly isolated: it may write only the
     * temporary scope named by its change. Never turn a probe into a global
     * checkpoint of canonical event scopes. */
    if (!synthetic_change) {
        if (!mapp(change) || undefinedp(change["scope_key"]))
            foreach (scope_key in keys(_persisted_event_scopes || ([ ])))
                if (member_array(scope_key, scope_keys) == -1)
                    scope_keys += ({ scope_key });
        foreach (entry in values(_active || ([ ]))) {
            scope_key = query_active_entry_scope(entry);
            if (scope_key != "" && member_array(scope_key, scope_keys) == -1)
                scope_keys += ({ scope_key });
        }
        foreach (event_id, event in _events || ([ ])) {
            scope_key = query_event_scope_key(event, event["room"] || "");
            if (scope_key != "" && member_array(scope_key, scope_keys) == -1)
                scope_keys += ({ scope_key });
        }
    }
    pending_phase = 0;
    foreach (scope_key in scope_keys) {
        scope_result = persist_event_scope(scope_key, operation_id);
        if (scope_result == 1)
            continue;
        if (scope_result == 2) {
            pending_phase = 1;
            continue;
        }
        if (pending_phase) {
            _last_persist_result = "unknown";
            _last_persist_error = "world event has mixed persistence phases";
        } else {
            _last_persist_result = mapp(_last_event_save_phase)
                ? _last_event_save_phase["outcome"] : "unknown";
            _last_persist_error = mapp(_last_event_save_phase)
                ? (_last_event_save_phase["error"] || "")
                : "world event scope persistence failed";
        }
        return 0;
    }
    if (pending_phase) {
        _last_persist_result = "persisted_receipt_pending";
        _last_persist_error = "world event scope receipt pending";
        return 2;
    }
    if (sizeof(scope_keys) == 0) {
        file = query_save_file();
        if (file_size(file + ".o") > 0) {
            err = catch(rm(file + ".o"));
            if (err) {
                _last_persist_result = "failed";
                _last_persist_error = sprintf("%O", err);
                return 0;
            }
        }
    }
    /* Keep the legacy file as a read-only compatibility source until every
     * known scope has a shard; a crash between two scope writes must not
     * erase untouched legacy scopes. Shard tombstones supersede it on restore. */
    foreach (scope_key in scope_keys)
        _persisted_event_scopes[scope_key] = 1;
    _last_persist_result = "success";
    _last_persist_error = "";
    return 1;
}

private void schedule_event_retry()
{
    if (_persist_retry_handle || !_persist_pending
        || _persist_retry_count >= EVENT_RETRY_LIMIT)
        return;
    _persist_retry_handle = call_out("retry_pending_event", 5);
}

private int persist_candidate(mapping candidate, int tick_epoch,
    string operation_id, mapping change)
{
    mapping previous_active;
    mapping previous_tombstones;
    int previous_tick;
    int saved;

    if (!mapp(candidate) || tick_epoch < _last_tick_epoch)
        return 0;
    if (_persist_pending && mapp(change)
        && change["scope_key"] == _pending_scope_key)
        return 0;
    previous_active = _active;
    previous_tombstones = _event_tombstones;
    previous_tick = _last_tick_epoch;
    _active = candidate;
    _last_tick_epoch = tick_epoch;
    saved = persist_current(operation_id, change);
    if (saved == 1) {
        if (!_persist_pending || !mapp(change)
            || change["scope_key"] == _pending_scope_key) {
            _persist_pending = 0;
            _pending_tick_epoch = 0;
            _pending_scope_key = "";
            _pending_operation_id = "";
            _pending_active = 0;
            _pending_tombstones = 0;
            _pending_change = 0;
            _persist_retry_count = 0;
        }
        return 1;
    }
    _pending_active = deep_copy_value(candidate);
    _pending_tombstones = deep_copy_value(_event_tombstones);
    _pending_tick_epoch = tick_epoch;
    _pending_scope_key = mapp(change) && stringp(change["scope_key"])
        ? change["scope_key"] : "";
    _pending_operation_id = operation_id;
    _pending_change = mapp(change) ? deep_copy_value(change) : 0;
    _persist_pending = 1;
    _persist_retry_count = 0;
    if (!stringp(_last_persist_result)
        || _last_persist_result == "not_written") {
        _active = previous_active;
        _event_tombstones = previous_tombstones;
        _last_tick_epoch = previous_tick;
    }
    schedule_event_retry();
    return 0;
}

private void emit_change(mapping change, int tick_epoch, string operation_id,
    int recovered)
{
    mapping event;

    event = mapp(change) ? copy(change) : ([ "kind": "tick" ]);
    event["tick_epoch"] = tick_epoch;
    event["persisted"] = 1;
    event["recovered"] = recovered;
    event["operation_id"] = operation_id;
    event["timestamp"] = time();
    EVENT_D->emit("world.event.state_changed", event);
}

private void retry_pending_event()
{
    mapping previous_active;
    mapping previous_tombstones;
    mapping candidate;
    mapping change;
    mapping entry;
    mapping tombstones;
    string operation_id;
    string key;
    string scope_key;
    int previous_tick;
    int tick_epoch;
    int saved;

    _persist_retry_handle = 0;
    if (!_persist_pending || !mapp(_pending_active))
        return;
    candidate = deep_copy_value(_pending_active);
    change = mapp(_pending_change) ? copy(_pending_change) : 0;
    operation_id = _pending_operation_id;
    tick_epoch = _pending_tick_epoch;
    previous_active = _active;
    previous_tombstones = _event_tombstones;
    previous_tick = _last_tick_epoch;
    /* The failed candidate is a snapshot from the original operation. Other
     * scopes may have committed while this scope was waiting; replace the
     * non-pending scopes with their current in-memory state before retrying,
     * otherwise retrying A can silently roll back a successful B mutation. */
    foreach (key in keys(candidate || ([ ])))
        if (query_active_entry_scope(candidate[key]) != _pending_scope_key)
            map_delete(candidate, key);
    foreach (key, entry in _active || ([ ]))
        if (query_active_entry_scope(entry) != _pending_scope_key)
            candidate[key] = deep_copy_value(entry);
    _event_tombstones = deep_copy_value(_pending_tombstones || ([ ]));
    foreach (scope_key, tombstones in previous_tombstones || ([ ])) {
        if (scope_key == _pending_scope_key)
            continue;
        map_delete(_event_tombstones, scope_key);
        _event_tombstones[scope_key] = deep_copy_value(tombstones);
    }
    _active = candidate;
    _last_tick_epoch = tick_epoch;
    _persist_retry_count++;
    saved = persist_current(operation_id, change);
    if (saved == 1) {
        _persist_pending = 0;
        _pending_tick_epoch = 0;
        _pending_scope_key = "";
        _pending_operation_id = "";
        _pending_active = 0;
        _pending_tombstones = 0;
        _pending_change = 0;
        _persist_retry_count = 0;
        _last_catch_up_result = "recovered";
        ensure_all_active_objects();
        emit_change(change, tick_epoch, operation_id, 1);
        return;
    }
    if (!stringp(_last_persist_result)
        || _last_persist_result == "not_written") {
        _active = previous_active;
        _event_tombstones = previous_tombstones;
        _last_tick_epoch = previous_tick;
    }
    schedule_event_retry();
}

private void load_events()
{
    mapping definitions;
    mapping validation;
    mapping previous_events;
    int previous_generation;
    mixed err;

    previous_events = mapp(_events) ? _events : ([ ]);
    previous_generation = _event_generation;
    _previous_event_generation = previous_generation;
    validation = 0;
    err = catch(validation = CONTENT_REGISTRY_D->query_validation_report());
    if (err || !mapp(validation) || validation["ok"] != 1) {
        _last_validation_result = "rejected";
        _last_validation_error = err ? sprintf("%O", err)
            : "content event candidate failed validation";
        _events = previous_events;
        _event_generation = previous_generation;
        return;
    }

    definitions = CONTENT_REGISTRY_D->query_world_events();
    if (!mapp(definitions)) {
        _last_validation_result = "rejected";
        _last_validation_error = "world event definitions are not a mapping";
        _events = previous_events;
        _event_generation = previous_generation;
        return;
    }
    _events = definitions;
    /* 加载成功（含合法空集合）后置位：后续 reconcile 不再重复加载校验。 */
    _events_loaded = 1;
    _event_generation = CONTENT_REGISTRY_D->query_content_generation();
    _last_validation_result = "accepted";
    _last_validation_error = "";
}

void on_config_reloaded(string key, string config_name)
{
    /* 内容热更后定义可能变化，允许下一次 reconcile 重新加载并校验。 */
    _events_loaded = 0;
    if (key == "content" || config_name == "content")
        load_events();
}

void on_config_reload_rollback(string key, string config_name)
{
    if (key == "content" || config_name == "content")
        load_events();
}

private int event_hour_matches(mapping event, int hour)
{
    mixed hours;

    hours = event["hours"];
    if (intp(hours))
        return hours == hour;
    if (arrayp(hours))
        return member_array(hour, hours) != -1;
    return 0;
}

private object resolve_event_room(string room_path)
{
    if (!stringp(room_path) || room_path == "")
        return 0;
    return WORLD_TRAVEL_D->resolve_public_room(room_path, ([
        "reason": "world_event",
    ]));
}

private void destroy_event_objects(object *objects)
{
    object ob;

    if (!arrayp(objects))
        return;
    foreach (ob in objects)
        if (objectp(ob))
            destruct(ob);
}

private void ensure_all_active_objects()
{
    mapping entry;
    mapping event;
    object room;
    object ob;
    string key;
    string event_id;
    string present_id;

    if (!mapp(_active))
        return;
    foreach (key, entry in _active) {
        if (!mapp(entry))
            continue;
        event_id = entry["event_id"] || "";
        event = _events[event_id];
        if (!mapp(event))
            continue;
        room = resolve_event_room(entry["room_path"] || event["room"] || "");
        if (!objectp(room))
            continue;
        present_id = entry["present_id"] || event["present_id"] || event_id;
        if (event["single_use"] == 1
            && (entry["consumed"] == 1 || entry["pickup_pending"] == 1))
            continue;
        ob = call_other("/lib/std/utils/util_view", "ensure_room_object",
            room, present_id, event["path"] || "");
        if (objectp(ob) && function_exists("apply_event_context", ob))
            ob->apply_event_context(event_id, event);
    }
}

/* 一次性奇遇物品的拾取预留、提交和释放与活动实体同一 scope 持久化。
 * 预留先落盘，避免物品转移失败或进程中断时事件无消费账本；下一次窗口
 * reconcile 删除旧 entry，再创建全新的周期。 */
private varargs int update_item_pickup_state(string event_id, object item,
    object source_room, string operation)
{
    mapping entry;
    mapping previous_entry;
    mapping event;
    object room;
    string key;
    string room_path;
    string scope_key;
    string operation_id;

    if (!stringp(event_id) || event_id == "" || !objectp(item))
        return 0;
    event = mapp(_events) ? _events[event_id] : 0;
    if (!mapp(event) || event["single_use"] != 1)
        return 1;
    room = objectp(source_room) ? source_room : environment(item);
    if (!objectp(room))
        return 0;
    room_path = base_name(room);
    foreach (key, entry in _active || ([ ])) {
        if (!mapp(entry) || entry["event_id"] != event_id
            || entry["room_path"] != room_path)
            continue;
        if (operation == "reserve" &&
            (entry["consumed"] == 1 || entry["pickup_pending"] == 1))
            return 0;
        if (operation == "commit" && entry["pickup_pending"] != 1)
            return entry["consumed"] == 1 ? 1 : 0;
        if (operation == "release" && entry["pickup_pending"] != 1)
            return entry["consumed"] == 0 ? 1 : 0;
        scope_key = query_active_entry_scope(entry);
        if (scope_key == "")
            return 0;
        previous_entry = deep_copy_value(entry);
        if (operation == "reserve") {
            entry["consumed"] = 1;
            entry["pickup_pending"] = 1;
        } else if (operation == "commit") {
            entry["pickup_pending"] = 0;
        } else if (operation == "release") {
            entry["consumed"] = 0;
            entry["pickup_pending"] = 0;
        } else {
            return 0;
        }
        operation_id = next_operation_id("item_" + operation);
        if (persist_event_scope(scope_key, operation_id) != 1) {
            if (!mapp(_last_event_save_phase)
                || _last_event_save_phase["outcome"] == "not_written")
                _active[key] = previous_entry;
            return 0;
        }
        return 1;
    }
    return 0;
}

varargs int reserve_item_pickup(string event_id, object item, object source_room)
{
    return update_item_pickup_state(event_id, item, source_room, "reserve");
}

varargs int commit_item_pickup(string event_id, object item, object source_room)
{
    return update_item_pickup_state(event_id, item, source_room, "commit");
}

varargs int release_item_pickup(string event_id, object item, object source_room)
{
    return update_item_pickup_state(event_id, item, source_room, "release");
}

void on_game_time_hour_changed(string event_name, mixed data)
{
    int hour;

    if (!mapp(data) || !intp(data["hour"]))
        return;
    hour = data["hour"];
    if (hour < 0 || hour >= TIME_D->query_hours_per_day())
        return;
    if (!reconcile_events(hour, time(), 0) && _persist_pending)
        _last_catch_up_result = "pending_retry";
}

private int reconcile_events(int hour, int tick_epoch, int catch_up)
{
    mapping candidate;
    mapping event;
    mapping entry;
    mapping change;
    mapping scope_changes;
    mapping scope_candidate;
    mapping scope_change;
    mapping staged_by_scope;
    mapping removed_by_scope;
    object room;
    object ob;
    object existing;
    object *scope_objects;
    object *scope_removed;
    string event_id;
    string key;
    string room_path;
    string present_id;
    string scope_key;
    string *scope_keys;
    string *successful_added;
    string operation_id;
    int matched;
    int saved;
    int all_saved;

    /* L-75：合法空事件集合不能让每 tick 重新加载校验；
     * 用显式的“已加载”状态判断，首次加载仍然完整校验内容。 */
    if (!_events_loaded)
        load_events();
    candidate = deep_copy_value(_active || ([ ]));
    if (!mapp(candidate))
        return 0;
    change = ([
        "kind": "reconcile",
        "added": ({ }),
        "removed": ({ }),
        "catch_up": catch_up ? 1 : 0,
    ]);
    scope_changes = ([ ]);
    staged_by_scope = ([ ]);
    removed_by_scope = ([ ]);
    successful_added = ({ });
    all_saved = 1;

    /* First remove expired or deleted definitions. */
    foreach (key, entry in _active || ([ ])) {
        event_id = mapp(entry) ? entry["event_id"] : "";
        event = mapp(_events) ? _events[event_id] : 0;
        if (mapp(event) && event_hour_matches(event, hour))
            continue;
        map_delete(candidate, key);
        if (mapp(entry)) {
            scope_key = query_active_entry_scope(entry);
            if (scope_key != "") {
                if (!mapp(scope_changes[scope_key]))
                    scope_changes[scope_key] = ([
                        "kind": "reconcile",
                        "scope_key": scope_key,
                        "added": ({ }),
                        "removed": ({ }),
                        "catch_up": catch_up ? 1 : 0,
                    ]);
                scope_changes[scope_key]["removed"] += ({ event_id });
                room = resolve_event_room(entry["room_path"] || "");
                if (objectp(room) && stringp(entry["present_id"])) {
                    existing = present(entry["present_id"], room);
                    if (objectp(existing)) {
                        scope_removed = removed_by_scope[scope_key] || ({ });
                        removed_by_scope[scope_key] = scope_removed + ({ existing });
                    }
                }
            }
        }
        change["removed"] += ({ event_id });
    }

    /* Then stage every currently matching event. Each event/room pair owns
     * its own active record, so a future instance room cannot overwrite it. */
    foreach (event_id, event in _events) {
        if (!mapp(event) || !event_hour_matches(event, hour))
            continue;
        room_path = event["room"] || "";
        room = resolve_event_room(room_path);
        if (!objectp(room))
            continue;
        room_path = base_name(room);
        key = active_key(event_id, room_path);
        if (mapp(candidate[key]))
            continue;
        scope_key = query_event_scope_key(event, room_path);
        if (scope_key == "")
            continue;
        present_id = event["present_id"] || event_id;
        existing = present(present_id, room);
        ob = call_other("/lib/std/utils/util_view", "ensure_room_object",
            room, present_id, event["path"] || "");
        if (!objectp(ob))
            continue;
        if (function_exists("apply_event_context", ob))
            ob->apply_event_context(event_id, event);
        candidate[key] = ([
            "event_id": event_id,
            "room_path": room_path,
            "scope_key": scope_key,
            "present_id": present_id,
            "path": event["path"] || "",
            "single_use": event["single_use"] == 1 ? 1 : 0,
            "consumed": 0,
            "pickup_pending": 0,
            "world_generation": WORLD_STATE_D->query_world_generation(),
        ]);
        if (!objectp(existing)) {
            scope_objects = staged_by_scope[scope_key] || ({ });
            staged_by_scope[scope_key] = scope_objects + ({ ob });
        }
        if (!mapp(scope_changes[scope_key]))
            scope_changes[scope_key] = ([
                "kind": "reconcile",
                "scope_key": scope_key,
                "added": ({ }),
                "removed": ({ }),
                "catch_up": catch_up ? 1 : 0,
            ]);
        scope_changes[scope_key]["added"] += ({ event_id });
        change["added"] += ({ event_id });
        matched++;
    }

    scope_keys = keys(scope_changes || ([ ]));
    foreach (scope_key in keys(_persisted_event_scopes || ([ ])))
        if (member_array(scope_key, scope_keys) == -1)
            scope_keys += ({ scope_key });
    foreach (event_id, event in _events || ([ ])) {
        scope_key = query_event_scope_key(event, event["room"] || "");
        if (scope_key != "" && member_array(scope_key, scope_keys) == -1)
            scope_keys += ({ scope_key });
    }
    foreach (entry in values(candidate || ([ ]))) {
        scope_key = query_active_entry_scope(entry);
        if (scope_key != "" && member_array(scope_key, scope_keys) == -1)
            scope_keys += ({ scope_key });
    }
    foreach (scope_key in scope_keys) {
        scope_candidate = deep_copy_value(_active || ([ ]));
        if (!mapp(scope_candidate)) {
            all_saved = 0;
            continue;
        }
        foreach (key, entry in scope_candidate)
            if (query_active_entry_scope(entry) == scope_key)
                map_delete(scope_candidate, key);
        foreach (key, entry in candidate)
            if (query_active_entry_scope(entry) == scope_key)
                scope_candidate[key] = deep_copy_value(entry);
        scope_change = mapp(scope_changes[scope_key])
            ? scope_changes[scope_key] : ([
                "kind": "reconcile",
                "scope_key": scope_key,
                "added": ({ }),
                "removed": ({ }),
                "catch_up": catch_up ? 1 : 0,
            ]);
        operation_id = next_operation_id(catch_up ? "catch-up" : "tick");
        saved = persist_candidate(scope_candidate, tick_epoch,
            operation_id, scope_change);
        if (saved) {
            destroy_event_objects(removed_by_scope[scope_key] || ({ }));
            successful_added += scope_change["added"] || ({ });
            emit_change(scope_change, tick_epoch, operation_id, 0);
        } else {
            all_saved = 0;
            destroy_event_objects(staged_by_scope[scope_key] || ({ }));
        }
    }
    if (!all_saved)
        return 0;
    foreach (event_id in successful_added) {
        event = _events[event_id];
        if (mapp(event) && stringp(event["broadcast_text"])
            && event["broadcast_text"] != "")
            msg_broadcast(event["broadcast_text"]);
    }
    _last_catch_up_result = catch_up ? "applied" : "not_needed";
    if (catch_up)
        _catch_up_count = matched;
    return 1;
}

void create()
{
    ::create();
    _events = ([ ]);
    _event_tombstones = ([ ]);
    _active = ([ ]);
    _event_scope_tombstones = ([ ]);
    _persisted_event_scopes = ([ ]);
    _event_scope_key = "";
    _event_scope_tick_epoch = 0;
    _event_scope_deleted = 0;
    _last_validation_result = "not_run";
    _last_validation_error = "";
    _synthetic_fail_scopes = ([ ]);
    _event_generation = 0;
    _previous_event_generation = 0;
    EVENT_D->on("game.time.hour_changed", this_object(),
        "on_game_time_hour_changed", 0);
    CONFIG_D->compat_register_config_callback("content", "on_config_reloaded", this_object());
    restore_event_state();
    /* Restart reconciliation is a bounded catch-up: one current-hour pass,
     * never an unbounded replay of every missed 30-second tick. */
    catch(reconcile_events(TIME_D->query_hour(), time(), 1));
    schedule_next_tick();
}

int synthetic_fail_next_scope_persist(string scope_key)
{
    if (!synthetic_scope_valid(scope_key))
        return 0;
    if (!mapp(_synthetic_fail_scopes))
        _synthetic_fail_scopes = ([ ]);
    _synthetic_fail_scopes[scope_key] = 1;
    return 1;
}

mapping synthetic_retry_pending()
{
    string scope_key;

    if (!_persist_pending)
        return ([ "ok": 1, "pending": 0, "recovered": 0 ]);
    scope_key = _pending_scope_key;
    retry_pending_event();
    return ([
        "ok": !_persist_pending,
        "pending": _persist_pending ? 1 : 0,
        "recovered": !_persist_pending ? 1 : 0,
        "scope_key": scope_key,
        "last_persist_result": _last_persist_result,
    ]);
}

mapping synthetic_activate_scope_event(string scope_key, string event_id)
{
    mapping candidate;
    mapping change;
    mapping entry;
    mapping tombstones;
    string key;
    int generation;

    if (!registered_scope(scope_key) || !stringp(event_id) || event_id == "")
        return ([ "ok": 0, "error": "scope_or_event_invalid" ]);
    tombstones = mapp(_event_tombstones[scope_key])
        ? _event_tombstones[scope_key] : ([ ]);
    if (mapp(tombstones[event_id]))
        return ([ "ok": 0, "state": "tombstoned", "event_id": event_id ]);
    key = synthetic_event_key(scope_key, event_id);
    entry = mapp(_active) ? _active[key] : 0;
    if (mapp(entry))
        return ([ "ok": 1, "duplicate": 1, "state": "active",
            "event_id": event_id,
            "event_generation": entry["event_generation"] || 0 ]);

    candidate = deep_copy_value(_active || ([ ]));
    if (!mapp(candidate))
        return ([ "ok": 0, "error": "candidate_copy_failed" ]);
    generation = 1;
    candidate[key] = ([
        "event_id": event_id,
        "room_path": "",
        "scope_key": scope_key,
        "present_id": "",
        "path": "",
        "synthetic": 1,
        "event_generation": generation,
        "world_generation": WORLD_STATE_D->query_world_generation(),
        "created_at": time(),
    ]);
    change = ([
        "kind": "synthetic_event",
        "scope_key": scope_key,
        "event_id": event_id,
        "event_generation": generation,
    ]);
    if (!persist_candidate(candidate, time(),
        "world-event/synthetic/" + scope_key + "/" + event_id,
        change))
        return ([ "ok": 0, "error": "persistence_failed",
            "recovery_required": _persist_pending ? 1 : 0,
            "scope_key": scope_key, "event_id": event_id ]);
    return ([ "ok": 1, "state": "active", "duplicate": 0,
        "scope_key": scope_key, "event_id": event_id,
        "event_generation": generation,
        "operation_id": "world-event/synthetic/" + scope_key + "/" + event_id ]);
}

mapping synthetic_purge_scope_event(string scope_key, string event_id)
{
    mapping candidate;
    mapping previous_tombstones;
    mapping tombstones;
    mapping entry;
    string key;
    int removed;

    if (!registered_scope(scope_key) || !stringp(event_id) || event_id == "")
        return ([ "ok": 0, "error": "scope_or_event_invalid" ]);
    candidate = deep_copy_value(_active || ([ ]));
    previous_tombstones = mapp(_event_tombstones[scope_key])
        ? deep_copy_value(_event_tombstones[scope_key]) : ([ ]);
    tombstones = mapp(_event_tombstones[scope_key])
        ? _event_tombstones[scope_key] : ([ ]);
    foreach (key, entry in candidate) {
        if (!mapp(entry) || entry["scope_key"] != scope_key
            || entry["event_id"] != event_id)
            continue;
        map_delete(candidate, key);
        removed = 1;
    }
    if (mapp(tombstones[event_id]))
        return ([ "ok": 1, "duplicate": 1, "state": "tombstoned",
            "event_id": event_id ]);
    if (!removed)
        return ([ "ok": 0, "error": "event_not_active", "event_id": event_id ]);
    tombstones[event_id] = ([
        "event_id": event_id,
        "scope_key": scope_key,
        "event_generation": 1,
        "world_generation": WORLD_STATE_D->query_world_generation(),
        "deleted_at": time(),
    ]);
    _event_tombstones[scope_key] = tombstones;
    if (!persist_candidate(candidate, time(),
        "world-event/synthetic-purge/" + scope_key + "/" + event_id,
        ([
            "kind": "synthetic_event_purge",
            "scope_key": scope_key,
            "event_id": event_id,
            "tombstone": 1,
        ]))) {
        if (sizeof(previous_tombstones) > 0)
            _event_tombstones[scope_key] = previous_tombstones;
        else
            map_delete(_event_tombstones, scope_key);
        return ([ "ok": 0, "error": "persistence_failed",
            "recovery_required": _persist_pending ? 1 : 0,
            "scope_key": scope_key, "event_id": event_id ]);
    }
    return ([ "ok": 1, "state": "tombstoned", "duplicate": 0,
        "scope_key": scope_key, "event_id": event_id ]);
}

int clear_synthetic_scope(string scope_key)
{
    mapping entry;
    string file;
    string key;
    mixed err;

    if (!stringp(scope_key) || strsrch(scope_key, "shared:synthetic_") != 0)
        return 0;
    if (_persist_pending && _pending_scope_key == scope_key) {
        _persist_pending = 0;
        _pending_tick_epoch = 0;
        _pending_scope_key = "";
        _pending_operation_id = "";
        _pending_active = 0;
        _pending_tombstones = 0;
        _pending_change = 0;
        _persist_retry_count = 0;
        if (_persist_retry_handle) {
            remove_call_out(_persist_retry_handle);
            _persist_retry_handle = 0;
        }
    }
    foreach (key, entry in _active || ([ ])) {
        if (!mapp(entry) || entry["scope_key"] != scope_key)
            continue;
        if (entry["synthetic"] != 1)
            return 0;
        map_delete(_active, key);
    }
    map_delete(_event_tombstones, scope_key);
    map_delete(_persisted_event_scopes, scope_key);
    map_delete(_synthetic_fail_scopes, scope_key);
    file = query_scope_save_file(scope_key) + ".o";
    err = file_size(file) > 0 ? catch(rm(file)) : 0;
    return !err;
}

mapping query_synthetic_scope_snapshot(string scope_key)
{
    mapping result;
    mapping entry;
    mapping active;
    mapping tombstones;
    mapping tombstone;
    string key;
    int max_generation;

    if (!registered_scope(scope_key))
        return ([ "ok": 0, "error": "scope_not_registered" ]);
    active = ([ ]);
    foreach (key, entry in _active || ([ ]))
        if (mapp(entry) && entry["scope_key"] == scope_key
            && entry["synthetic"] == 1) {
            active[key] = deep_copy_value(entry);
            if (entry["event_generation"] > max_generation)
                max_generation = entry["event_generation"];
        }
    tombstones = mapp(_event_tombstones[scope_key])
        ? deep_copy_value(_event_tombstones[scope_key]) : ([ ]);
    foreach (tombstone in values(tombstones || ([ ])))
        if (mapp(tombstone) && tombstone["event_generation"] > max_generation)
            max_generation = tombstone["event_generation"];
    result = ([
        "ok": 1,
        "scope_key": scope_key,
        "active": active,
        "active_count": sizeof(active),
        "tombstones": tombstones,
        "tombstone_count": sizeof(tombstones),
        "event_generation": max_generation,
        "world_generation": WORLD_STATE_D->query_world_generation(),
        "persist_pending": _persist_pending
            && _pending_scope_key == scope_key ? 1 : 0,
        "last_persist_result": _last_persist_result,
    ]);
    return result;
}

mapping query_active_event_for_room(object room)
{
    mapping entry;
    mapping event;
    string key;
    string room_path;
    string event_id;

    if (!objectp(room))
        return 0;
    room_path = base_name(room);
    if (room_path == "")
        return 0;
    foreach (key, entry in _active || ([ ])) {
        if (!mapp(entry) || entry["room_path"] != room_path)
            continue;
        event_id = entry["event_id"] || "";
        event = _events[event_id];
        if (!mapp(event))
            continue;
        if (event["single_use"] == 1
            && (entry["consumed"] == 1 || entry["pickup_pending"] == 1))
            continue;
        event = copy(event);
        event["id"] = event_id;
        event["room"] = room_path;
        event["scope_key"] = entry["scope_key"] || query_active_entry_scope(entry);
        event["present_id"] = entry["present_id"] || event["present_id"];
        event["path"] = entry["path"] || event["path"];
        return event;
    }
    return 0;
}

int ensure_event_present(object room)
{
    mapping event;
    object *inv;
    object ob;
    string present_id;

    if (!objectp(room))
        return 0;
    event = query_active_event_for_room(room);
    if (!mapp(event)) {
        inv = all_inventory(room);
        foreach (ob in inv) {
            if (!objectp(ob) || userp(ob) || interactive(ob))
                continue;
            if (function_exists("apply_event_context", ob)
                && environment(ob) == room)
                destruct(ob);
        }
        return 0;
    }
    present_id = event["present_id"] || event["id"] || "";
    if (present_id == "")
        return 0;
    ob = call_other("/lib/std/utils/util_view", "ensure_room_object",
        room, present_id, event["path"] || "");
    if (!objectp(ob))
        return 0;
    if (function_exists("apply_event_context", ob))
        ob->apply_event_context(event["id"], event);
    return 1;
}

private void check_events()
{
    int hour;

    _tick_handle = 0;
    hour = TIME_D->query_hour();
    if (!reconcile_events(hour, time(), 0) && _persist_pending)
        _last_catch_up_result = "pending_retry";
    schedule_next_tick();
}

mapping query_runtime_status()
{
    return ([
        "active_event_ids": mapp(_active) ? keys(_active) : ({ }),
        "active_instances": mapp(_active) ? deep_copy_value(_active) : ([ ]),
        "active_count": mapp(_active) ? sizeof(_active) : 0,
        "active_scope_keys": query_active_scope_keys(),
        "persisted_event_scopes": keys(_persisted_event_scopes || ([ ])),
        "scope_persistence_isolated": 1,
        "last_tick_epoch": _last_tick_epoch,
        "catch_up_count": _catch_up_count,
        "catch_up_result": _last_catch_up_result,
        "persist_pending": _persist_pending,
        "pending_scope_key": _pending_scope_key,
        "pending_operation_id": _pending_operation_id,
        "retry_count": _persist_retry_count,
        "last_persist_result": _last_persist_result,
        "last_persist_error": _last_persist_error,
        "event_generation": _event_generation,
        "last_validation_result": _last_validation_result,
        "last_validation_error": _last_validation_error,
        "previous_generation": _previous_event_generation,
    ]);
}

int query_last_tick_epoch()
{
    return _last_tick_epoch;
}

int query_active_event_count()
{
    return mapp(_active) ? sizeof(_active) : 0;
}

private void schedule_next_tick()
{
    if (_tick_handle)
        remove_call_out(_tick_handle);
    _tick_handle = call_out("check_events", EVENT_TICK_SECONDS);
}
