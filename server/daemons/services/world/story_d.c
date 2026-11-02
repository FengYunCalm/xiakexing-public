/**
 * @file    story_d.c
 * @brief   剧情触发守护进程,管理剧情触发器注册表与状态。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <system/event_names.h>

inherit "/lib/framework/daemon";

nosave mapping _story_triggers;
nosave mapping _registry_status;

private mapping query_story_config()
{
    mapping config;

    config = CONFIG_D->query_full_config("world_story");
    return mapp(config) ? config : ([]);
}

private int validate_story_action(mapping action)
{
    string action_type;

    if (!mapp(action))
        return 0;

    action_type = action["type"] || "";
    if (!stringp(action_type) || action_type == "")
        return 0;

    switch (action_type) {
        case "spawn_request":
            return stringp(action["room"]) && action["room"] != "";

        case "refresh_player_world_state":
            return 1;
    }

    return 0;
}

private int validate_story_trigger(mapping trigger_def)
{
    mixed *actions;
    mixed action;

    if (!mapp(trigger_def))
        return 0;

    actions = trigger_def["actions"];
    if (!arrayp(actions) || sizeof(actions) == 0)
        return 0;

    foreach (action in actions) {
        if (!validate_story_action(action))
            return 0;
    }

    return 1;
}

private void mark_reload_failure(string error_message)
{
    if (!mapp(_registry_status))
        _registry_status = ([]);

    _registry_status["config_name"] = "world_story";
    _registry_status["last_attempt_at"] = time();
    _registry_status["last_reload_ok"] = 0;
    _registry_status["last_error"] = stringp(error_message) ? error_message : "unknown";
}

private void mark_reload_success()
{
    int version;

    if (!mapp(_registry_status))
        _registry_status = ([]);

    version = intp(_registry_status["config_version"]) ? _registry_status["config_version"] : 0;
    _registry_status["config_name"] = "world_story";
    _registry_status["config_version"] = version + 1;
    _registry_status["last_attempt_at"] = time();
    _registry_status["last_reload_at"] = time();
    _registry_status["last_reload_ok"] = 1;
    _registry_status["last_error"] = "";
}

void reload_registry()
{
    mapping config;
    mapping configured_stories;
    mapping previous_stories;
    mapping validated_stories;
    mapping trigger_def;
    string story_id;

    config = query_story_config();
    previous_stories = mapp(_story_triggers) ? copy(_story_triggers) : ([]);
    configured_stories = config["stories"];
    if (!mapp(configured_stories)) {
        mark_reload_failure("missing world story definitions");
        _story_triggers = previous_stories;
        return;
    }

    validated_stories = ([]);
    foreach (story_id, trigger_def in configured_stories) {
        if (!stringp(story_id) || story_id == "")
            continue;
        if (!validate_story_trigger(trigger_def))
            continue;

        validated_stories[story_id] = copy(trigger_def);
        validated_stories[story_id]["id"] = story_id;
    }

    if (sizeof(configured_stories) > 0 && !sizeof(validated_stories)) {
        mark_reload_failure("all world story definitions invalid");
        _story_triggers = previous_stories;
        return;
    }

    _story_triggers = validated_stories;
    mark_reload_success();
}

void create()
{
    ::create();

    if (!mapp(_story_triggers))
        _story_triggers = ([]);
    if (!mapp(_registry_status)) {
        _registry_status = ([
            "config_name": "world_story",
            "config_version": 0,
            "last_attempt_at": 0,
            "last_reload_at": 0,
            "last_reload_ok": 0,
            "last_error": "",
        ]);
    }

    reload_registry();
    CONFIG_D->compat_register_config_callback("world_story", "on_config_reloaded", this_object());
}

void on_config_reloaded(string key, string config_name)
{
    if (key != "world_story" && config_name != "world_story")
        return;

    reload_registry();
}

void on_config_reload_rollback(string key, string config_name)
{
    if (key != "world_story" && config_name != "world_story")
        return;

    reload_registry();
}

int register_story_trigger(string story_id, mapping trigger_def)
{
    if (!stringp(story_id) || story_id == "" || !validate_story_trigger(trigger_def))
        return 0;

    _story_triggers[story_id] = copy(trigger_def);
    _story_triggers[story_id]["id"] = story_id;
    return 1;
}

mapping query_story_trigger(string story_id)
{
    if (!stringp(story_id) || !mapp(_story_triggers[story_id]))
        return 0;
    return copy(_story_triggers[story_id]);
}

mapping query_all_story_triggers()
{
    return copy(_story_triggers || ([]));
}

mapping query_registry_snapshot()
{
    return ([
        "config_name": _registry_status["config_name"] || "world_story",
        "config_version": intp(_registry_status["config_version"]) ? _registry_status["config_version"] : 0,
        "last_attempt_at": intp(_registry_status["last_attempt_at"]) ? _registry_status["last_attempt_at"] : 0,
        "last_reload_at": intp(_registry_status["last_reload_at"]) ? _registry_status["last_reload_at"] : 0,
        "last_reload_ok": _registry_status["last_reload_ok"] ? 1 : 0,
        "last_error": stringp(_registry_status["last_error"]) ? _registry_status["last_error"] : "",
        "story_count": sizeof(_story_triggers || ([])),
        "story_ids": sort_array(keys(_story_triggers || ([])), 1),
        "stories": copy(_story_triggers || ([])),
    ]);
}

int trigger_story(object actor, string story_id, mapping context)
{
    mapping trigger;

    trigger = query_story_trigger(story_id);
    if (!mapp(trigger))
        return 0;

    EVENT_D->emit(EVENT_WORLD_STORY_TRIGGERED, ([
        "story_id": story_id,
        "actor": objectp(actor) ? get_object_id(actor) : "",
        "story": copy(trigger),
        "context": mapp(context) ? copy(context) : ([]),
        "timestamp": time()
    ]));
    return 1;
}
