/**
 * @file    player_session.c
 * @brief   玩家会话子模块



 *
 * @par Description:
 * 基础对象层子模块，供上层对象组合能力。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <system/xk_protocol.h>
#include <system/constants.h>

#define OWNER_READ_MODEL_TIMEOUT_MS 2000
#define SCORE_READ_MODEL_REFRESH_MAX_ATTEMPTS 2
#define GATEWAY_SESSION_TOUCH_INTERVAL_SECONDS 5
#define XK_PROTOCOL_CORE "/kernel/framework/xk_protocol_core"
#define ROOM_MAPREAD_XK "/kernel/simul_efun_room_mapread_xk"
#define ROOM_ENTITIES_XK "/kernel/simul_efun_room_entities_xk"

inherit "/lib/std/utils/util_owner_projection";

mixed query_temp(string path);
int set_temp(string path, mixed value);
int delete_temp(string path);
mixed query(string path);
int query_xk_inventory_generation();
string query_owner_hp_read_model_live_token();
string process_input(string str);
varargs void schedule_owner_hp_read_model_refresh(int force_refresh);
varargs void schedule_owner_score_read_model_refresh(int force_refresh);
varargs void schedule_owner_map_grid_read_model_refresh(int force_refresh);
private int owner_read_model_kind_supported(string projection_kind);
private int mark_owner_read_model_demand(string projection_kind);
private int query_owner_read_model_demand_generation(string projection_kind);
private mapping query_owner_thread_status();
private string query_owner_read_model_input_token(string projection_kind);
private void record_owner_read_model_claim(string projection_kind,
    int demand_generation);
private void cancel_owner_read_model_refresh_callout(string projection_kind);
private void record_owner_protocol_projection(string outcome, mapping context);
mapping owner_project_protocol_payload(mapping payload);
private mapping validate_owner_preencoded_result(string projection_kind,
    mapping result);

int query_xk_inventory_generation()
{
    mixed generation;

    generation = query_temp("xk/inventory_generation");
    return intp(generation) && generation >= 0 ? generation : 0;
}

int query_xk_score_projection_generation()
{
    mixed generation;

    generation = query_temp("xk/score_projection_generation");
    return intp(generation) && generation >= 0 ? generation : 0;
}

int query_xk_skill_projection_generation()
{
    mixed generation;

    generation = query_temp("xk/skill_projection_generation");
    return intp(generation) && generation >= 0 ? generation : 0;
}

/* Maxima freshness is player-local. HP/MP panels must not inherit unrelated
 * score churn, while the full vital snapshot also tracks EP changes. */
int query_xk_hp_maxima_generation()
{
    mixed generation;

    generation = query_temp("xk/hp_maxima_generation");
    return intp(generation) && generation >= 0 ? generation : 0;
}

int query_xk_vital_maxima_generation()
{
    mixed generation;

    generation = query_temp("xk/vital_maxima_generation");
    return intp(generation) && generation >= 0 ? generation : 0;
}

void invalidate_xk_skill_projection()
{
    set_temp("xk/skill_projection_generation",
        query_xk_skill_projection_generation() + 1);
}

varargs void invalidate_xk_vital_maxima_projection(int affects_hp_dialog)
{
    set_temp("xk/vital_maxima_generation",
        query_xk_vital_maxima_generation() + 1);
    if (!affects_hp_dialog)
        return;
    set_temp("xk/hp_maxima_generation",
        query_xk_hp_maxima_generation() + 1);
    schedule_owner_hp_read_model_refresh();
}

varargs void invalidate_xk_score_projection(int defer_refresh)
{
    set_temp("xk/score_projection_generation",
        query_xk_score_projection_generation() + 1);
    mark_owner_read_model_demand("score_dialog");
    if (!defer_refresh)
        schedule_owner_score_read_model_refresh();
}

private mapping query_owner_read_model_contexts()
{
    mapping contexts;

    contexts = query_temp("owner/read_model_contexts");
    return mapp(contexts) ? contexts : ([]);
}

private mapping query_owner_read_model_states()
{
    mapping states;

    states = query_temp("owner/read_model_states");
    return mapp(states) ? states : ([]);
}

private mapping query_owner_read_model_output_contexts()
{
    mapping contexts;

    contexts = query_temp("owner/read_model_output_contexts");
    return mapp(contexts) ? contexts : ([]);
}

private int query_owner_read_model_demand_generation(string projection_kind)
{
    mapping generations;
    mixed generation;

    generations = query_temp("owner/read_model_demand_generations");
    generation = mapp(generations) ? generations[projection_kind] : 0;
    return intp(generation) && generation > 0 ? generation : 0;
}

private int mark_owner_read_model_demand(string projection_kind)
{
    mapping generations;
    int generation;

    if (!owner_read_model_kind_supported(projection_kind))
        return 0;
    generations = query_temp("owner/read_model_demand_generations");
    if (!mapp(generations))
        generations = ([]);
    generation = query_owner_read_model_demand_generation(projection_kind) + 1;
    generations[projection_kind] = generation;
    set_temp("owner/read_model_demand_generations", generations);
    return generation;
}

private void record_owner_read_model_claim(string projection_kind,
    int demand_generation)
{
    mapping claimed;

    if (!owner_read_model_kind_supported(projection_kind)
        || demand_generation <= 0)
        return;
    claimed = query_temp("owner/read_model_claimed_generations");
    if (!mapp(claimed))
        claimed = ([]);
    if (!intp(claimed[projection_kind])
        || claimed[projection_kind] < demand_generation) {
        claimed[projection_kind] = demand_generation;
        set_temp("owner/read_model_claimed_generations", claimed);
    }
}

varargs int claim_owner_read_model_refresh(string projection_kind,
    int force_refresh)
{
    mapping claimed;
    int claimed_generation;
    int demand_generation;

    if (!owner_read_model_kind_supported(projection_kind))
        return 0;
    demand_generation = query_owner_read_model_demand_generation(projection_kind);
    if (force_refresh)
        return 1;
    if (demand_generation <= 0)
        return 0;
    claimed = query_temp("owner/read_model_claimed_generations");
    claimed_generation = mapp(claimed) && intp(claimed[projection_kind])
        ? claimed[projection_kind] : 0;
    if (claimed_generation >= demand_generation)
        return 0;
    record_owner_read_model_claim(projection_kind, demand_generation);
    return 1;
}

/* A login/checkpoint projection is a new client epoch. Read-model demand
 * generations describe business changes and may legitimately remain equal,
 * so projection invalidation must release only the old owner claims. The
 * next explicit refresh can then claim the same business generation again. */
void reset_owner_read_model_projection()
{
    delete_temp("owner/read_model_claimed_generations");
}

private int next_owner_read_model_context_id()
{
    mixed current;
    int context_id;

    current = query_temp("owner/read_model_next_context_id");
    context_id = intp(current) && current > 0 ? current : 1;
    set_temp("owner/read_model_next_context_id", context_id + 1);
    return context_id;
}

private int owner_read_model_kind_supported(string projection_kind)
{
    return projection_kind == "inventory" || projection_kind == "hp_dialog"
        || projection_kind == "score_dialog" || projection_kind == "map_grid";
}

private mixed *expected_owner_preencoded_stable_keys(string projection_kind)
{
    switch (projection_kind) {
    case "inventory":
        return ({ "items", "read_side_worker", "read_side_worker_state" });
    case "hp_dialog":
    case "score_dialog":
        return ({ "title", "content", "schema_version", "source", "dialog_id",
            "action_groups", "read_side_worker", "read_side_worker_state" });
    case "map_grid":
        return ({ });
    default:
        return ({ });
    }
}

private mixed *expected_owner_preencoded_dynamic_keys(string projection_kind)
{
    if (projection_kind == "hp_dialog" || projection_kind == "score_dialog")
        return ({ "part_index", "part_total", "final", "replace_mode" });
    if (projection_kind == "map_grid")
        return ({ "protocol_version", "projection_id",
            "projection_revision", "projection_generation",
            "room_id", "map_revision" });
    return ({ });
}

private int owner_map_grid_key_schema_valid(mixed *actual)
{
    mixed key;
    mixed *required;
    mixed *optional;
    mixed *seen;

    if (!arrayp(actual))
        return 0;

    required = ({ "rows", "paths", "rooms", "quest_markers",
        "quest_marker_details", "width", "height", "read_side_worker",
        "read_side_worker_state" });
    optional = ({ "title", "zone", "legend", "region_id",
        "map_context_id", "map_layer", "submap_id" });
    if (sizeof(actual) < sizeof(required)
        || sizeof(actual) > sizeof(required) + sizeof(optional))
        return 0;
    foreach (key in required) {
        if (member_array(key, actual) == -1)
            return 0;
    }
    seen = ({ });
    foreach (key in actual) {
        if (!stringp(key) || (member_array(key, required) == -1
            && member_array(key, optional) == -1)
            || member_array(key, seen) != -1)
            return 0;
        seen += ({ key });
    }
    return 1;
}

private int owner_preencoded_key_set_matches(mixed *actual, mixed *expected)
{
    mixed key;

    if (!arrayp(actual) || !arrayp(expected) || sizeof(actual) != sizeof(expected))
        return 0;
    foreach (key in actual) {
        if (!stringp(key) || member_array(key, expected) == -1)
            return 0;
    }
    foreach (key in expected) {
        if (!stringp(key) || member_array(key, actual) == -1)
            return 0;
    }
    return 1;
}

private mapping validate_owner_preencoded_result(string projection_kind,
    mapping result)
{
    mapping artifact;
    mapping dynamic_defaults;
    mapping view;
    mixed *expected_dynamic;
    mixed *expected_stable;
    string expected_type;

    if (!mapp(result) || !mapp(result["view"])
        || !mapp(result["preencoded"]))
        return 0;
    artifact = copy(result["preencoded"]);
    view = copy(result["view"]);
    expected_type = projection_kind == "inventory" ? XK_IVTY
        : projection_kind == "map_grid" ? XK_MAPS
        : projection_kind == "hp_dialog" || projection_kind == "score_dialog"
            ? XK_DLOG : "";
    if (artifact["schema"] != "xk/preencoded-payload-members/v1"
        || artifact["producer"] != "owner_project_protocol_payload/v1"
        || artifact["projection_kind"] != projection_kind
        || artifact["protocol_type"] != expected_type
        || !stringp(artifact["stable_json"])
        || !arrayp(artifact["stable_keys"])
        || !mapp(artifact["dynamic_defaults"]))
        return 0;
    if (strlen(artifact["stable_json"]) < 2
        || artifact["stable_json"][0] != '{'
        || artifact["stable_json"][<1] != '}')
        return 0;
    expected_stable = expected_owner_preencoded_stable_keys(projection_kind);
    if (projection_kind == "map_grid") {
        if (!owner_map_grid_key_schema_valid(artifact["stable_keys"]))
            return 0;
    } else if (!owner_preencoded_key_set_matches(
        artifact["stable_keys"], expected_stable))
        return 0;
    if (!stringp(view["read_side_worker_state"])
        || view["read_side_worker_state"] == "")
        return 0;
    if ((projection_kind == "hp_dialog" || projection_kind == "score_dialog")
        && (!stringp(view["content"]) || view["content"] == ""))
        return 0;
    dynamic_defaults = copy(artifact["dynamic_defaults"]);
    expected_dynamic = expected_owner_preencoded_dynamic_keys(projection_kind);
    if (!owner_preencoded_key_set_matches(keys(dynamic_defaults), expected_dynamic))
        return 0;

    artifact["dynamic_defaults"] = dynamic_defaults;
    artifact["validated"] = 1;
    return ([
        "view": view,
        "artifact": artifact,
    ]);
}

mapping publish_inline_read_model(string projection_kind, mapping payload,
    string input_token)
{
    mapping projected;
    mapping state;
    mapping states;
    mapping validated;
    string current_input_token;
    int previous_future_id;

    if (!owner_read_model_kind_supported(projection_kind)
        || !mapp(payload) || payload["projection_kind"] != projection_kind
        || !stringp(input_token) || input_token == "")
        return 0;
    current_input_token = query_owner_read_model_input_token(projection_kind);
    if (current_input_token == "" || current_input_token != input_token)
        return 0;

    payload = copy(payload);
    payload["read_side_worker"] = 0;
    payload["read_side_worker_state"] = "main_thread_native";
    if (mapp(payload["protocol_stable_payload"])) {
        payload["protocol_stable_payload"] = copy(
            payload["protocol_stable_payload"]);
        payload["protocol_stable_payload"]["read_side_worker"] = 0;
        payload["protocol_stable_payload"]["read_side_worker_state"] = "main_thread_native";
    }
    projected = owner_project_protocol_payload(payload);
    validated = validate_owner_preencoded_result(projection_kind, projected);
    current_input_token = query_owner_read_model_input_token(projection_kind);
    if (!mapp(validated) || current_input_token == ""
        || current_input_token != input_token)
        return 0;

    states = query_owner_read_model_states();
    state = mapp(states[projection_kind]) ? states[projection_kind] : 0;
    previous_future_id = mapp(state) && intp(state["future_id"])
        ? state["future_id"] : 0;
    states[projection_kind] = ([
        "projection_kind": projection_kind,
        "input_token": input_token,
        "demand_generation": query_owner_read_model_demand_generation(
            projection_kind),
        "future_id": 0,
        "read_side_worker_state": "main_thread_native",
        "state": "ready",
        "published_at": time(),
        "view": copy(validated["view"]),
        "preencoded": copy(validated["artifact"]),
    ]);
    set_temp("owner/read_model_states", states);
    record_owner_read_model_claim(projection_kind,
        query_owner_read_model_demand_generation(projection_kind));
    cancel_owner_read_model_refresh_callout(projection_kind);
    if (previous_future_id > 0) {
        owner_future_cancel(previous_future_id,
            "inline artifact superseded owner future");
        record_owner_protocol_projection("owner_cancelled", ([
            "projection_kind": projection_kind,
            "input_token": input_token,
        ]));
    }
    record_owner_protocol_projection("inline_completed", ([
        "projection_kind": projection_kind,
        "input_token": input_token,
    ]));
    return ([
        "state": "ready",
        "view": copy(validated["view"]),
        "preencoded": copy(validated["artifact"]),
    ]);
}

mapping publish_inline_inventory_read_model(mapping payload, string input_token)
{
    mapping result;

    if (!mapp(payload) || payload["projection_kind"] != "inventory")
        return 0;
    result = publish_inline_read_model("inventory", payload, input_token);
    if (!mapp(result))
        return 0;
    call_other("/kernel/simul_efun_room_entities_xk",
        "cancel_inventory_read_model_refresh", this_object());
    return result;
}

private mapping query_owner_thread_status()
{
    mapping status;
    mixed err;

    err = catch(status = call_other(
        "/daemons/services/system/multicore/worker_driver_adapter",
        "query_owner_thread_status"));
    return err || !mapp(status) ? 0 : status;
}

private string query_owner_hp_signature()
{
    return CLIENT_PROJECTION_D->query_hp_projection_live_token(this_object());
}

string query_owner_hp_read_model_live_token()
{
    return query_owner_hp_signature();
}

private string query_owner_read_model_input_token(string projection_kind)
{
    switch (projection_kind) {
    case "inventory":
        return sprintf("%d", query_xk_inventory_generation());
    case "hp_dialog":
        return query_owner_hp_signature();
    case "score_dialog":
        return CLIENT_PROJECTION_D->query_score_projection_live_token(
            this_object());
    case "map_grid":
        return call_other(ROOM_MAPREAD_XK,
            "xk_query_map_grid_read_model_token", this_object());
    default:
        return "";
    }
}

private void record_owner_protocol_projection(string outcome, mapping context)
{
    mapping counters;
    string projection_kind;

    counters = query_temp("owner/protocol_projection_counters");
    if (!mapp(counters))
        counters = ([]);
    if (!stringp(outcome) || outcome == "")
        outcome = "unknown";
    counters[outcome] = (intp(counters[outcome]) ? counters[outcome] : 0) + 1;
    counters["last_outcome"] = outcome;
    projection_kind = mapp(context) && stringp(context["projection_kind"])
        ? context["projection_kind"] : "";
    counters["last_projection_kind"] = projection_kind;
    set_temp("owner/protocol_projection_counters", counters);
    GATEWAY_SESSION_D->record_owner_protocol_projection(projection_kind, outcome);
}

private void record_owner_protocol_projection_publish_guard(string outcome)
{
    GATEWAY_SESSION_D->record_owner_protocol_projection_publish_guard(outcome);
}

/** 将玩家 read-model 阶段计时转发到全局低基数观测。 */
void record_owner_protocol_projection_timing(string phase, mapping context,
    int elapsed_ns)
{
    string projection_kind;

    projection_kind = mapp(context) && stringp(context["projection_kind"])
        ? context["projection_kind"] : "";
    GATEWAY_SESSION_D->record_owner_protocol_projection_timing(
        projection_kind, phase, elapsed_ns);
}

void record_owner_protocol_sync_fallback(string projection_kind, string reason)
{
    record_owner_protocol_projection("sync_fallback", ([
        "projection_kind": stringp(projection_kind) ? projection_kind : "",
        "reason": stringp(reason) ? reason : "",
    ]));
}

private void rollback_owner_read_model_future(int future_id, string reason)
{
    if (future_id > 0) {
        owner_future_cancel(future_id, reason);
        owner_future_take(future_id);
    }
}

varargs mapping query_owner_read_model(string projection_kind, string input_token,
    int max_age_seconds)
{
    mapping response;
    mapping state;
    mapping states;
    int started_at;

    if (!owner_read_model_kind_supported(projection_kind)
        || !stringp(input_token) || input_token == "")
        return ([ "state": "missing" ]);

    started_at = perf_counter_ns();
    states = query_owner_read_model_states();
    state = mapp(states[projection_kind]) ? states[projection_kind] : 0;
    if (!mapp(state)) {
        mark_owner_read_model_demand(projection_kind);
        return ([ "state": "missing" ]);
    }
    if (state["input_token"] != input_token) {
        mark_owner_read_model_demand(projection_kind);
        return ([ "state": "stale" ]);
    }
    if (state["state"] == "ready" && max_age_seconds > 0
        && (!intp(state["published_at"])
            || time() - state["published_at"] > max_age_seconds)) {
        mark_owner_read_model_demand(projection_kind);
        return ([ "state": "stale" ]);
    }

    response = copy(state);
    if (mapp(state["view"]))
        response["view"] = copy(state["view"]);
    if (mapp(state["preencoded"]))
        response["preencoded"] = copy(state["preencoded"]);
    if (state["state"] == "ready") {
        record_owner_protocol_projection("current_hit", state);
        record_owner_protocol_projection_timing("current_hit", state,
            perf_counter_ns() - started_at);
    } else if (state["state"] == "pending")
        record_owner_protocol_projection("pending_cached", state);
    return response;
}

varargs int submit_owner_read_model(string projection_kind, mapping payload,
    string input_token, int force_refresh)
{
    mapping context;
    mapping contexts;
    mapping result;
    mapping state;
    mapping states;
    mapping thread_status;
    int context_id;
    int future_id;
    int previous_future_id;
    int submit_started_at;

    thread_status = query_owner_thread_status();
    if (!mapp(thread_status) || thread_status["enabled"] != 1
        || thread_status["stopping"] == 1)
        return 0;
    if (!owner_read_model_kind_supported(projection_kind)
        || !mapp(payload) || !stringp(input_token) || input_token == "")
        return 0;

    states = query_owner_read_model_states();
    state = mapp(states[projection_kind]) ? states[projection_kind] : 0;
    if (!force_refresh && mapp(state) && state["input_token"] == input_token
        && state["state"] == "pending") {
        record_owner_protocol_projection("pending_cached", state);
        return 1;
    }
    if (!force_refresh && mapp(state) && state["input_token"] == input_token
        && state["state"] == "ready" && mapp(state["preencoded"])) {
        record_owner_protocol_projection("current_hit", state);
        return 1;
    }
    previous_future_id = mapp(state) && intp(state["future_id"])
        ? state["future_id"] : 0;

    submit_started_at = perf_counter_ns();
    result = owner_call_async(this_object(), "owner_project_protocol_payload",
        copy(payload));
    future_id = mapp(result) && intp(result["future_id"])
        ? result["future_id"] : 0;
    if (!mapp(result) || result["success"] != 1
        || future_id <= 0) {
        rollback_owner_read_model_future(future_id,
            "owner read model submit failed");
        record_owner_protocol_projection("owner_failed", ([
            "projection_kind": projection_kind,
            "input_token": input_token,
        ]));
        record_owner_protocol_projection_timing("submit", ([
            "projection_kind": projection_kind,
        ]), perf_counter_ns() - submit_started_at);
        return 0;
    }

    context_id = next_owner_read_model_context_id();
    context = ([
        "projection_kind": projection_kind,
        "input_token": input_token,
        "context_id": context_id,
        "demand_generation": query_owner_read_model_demand_generation(
            projection_kind),
        "read_side_worker_state": "ready",
    ]);
    context["future_id"] = future_id;
    contexts = query_owner_read_model_contexts();
    contexts[sprintf("%d", context_id)] = context;
    set_temp("owner/read_model_contexts", contexts);

    if (!gateway_future_watch(this_object(), context_id,
        future_id, OWNER_READ_MODEL_TIMEOUT_MS)) {
        contexts = query_owner_read_model_contexts();
        map_delete(contexts, sprintf("%d", context_id));
        set_temp("owner/read_model_contexts", contexts);
        rollback_owner_read_model_future(future_id,
            "owner read model watch failed");
        record_owner_protocol_projection("owner_failed", context);
        record_owner_protocol_projection_timing("submit", context,
            perf_counter_ns() - submit_started_at);
        return 0;
    }

    states[projection_kind] = copy(context) + ([
        "state": "pending",
        "submitted_at": time(),
    ]);
    set_temp("owner/read_model_states", states);
    record_owner_read_model_claim(projection_kind,
        context["demand_generation"]);

    if (previous_future_id > 0 && previous_future_id != future_id) {
        owner_future_cancel(previous_future_id,
            "owner read model superseded");
        record_owner_protocol_projection("owner_cancelled", context);
    }
    record_owner_protocol_projection_timing("submit", context,
        perf_counter_ns() - submit_started_at);
    return 1;
}

varargs int publish_read_model_artifact(string projection_kind, mapping payload,
    string input_token, int force_refresh)
{
    mapping thread_status;

    thread_status = query_owner_thread_status();
    if (mapp(thread_status) && thread_status["enabled"] == 1
        && thread_status["stopping"] != 1)
        return submit_owner_read_model(projection_kind, payload, input_token,
            force_refresh);
    if (projection_kind == "inventory")
        return mapp(publish_inline_inventory_read_model(payload, input_token));
    return mapp(publish_inline_read_model(projection_kind, payload, input_token));
}

int submit_owner_read_model_output(string projection_kind, mapping payload,
    string input_token, string protocol_type)
{
    mapping context;
    mapping contexts;
    mapping reservation;
    mapping result;
    mapping state;
    mapping states;
    mapping thread_status;
    int future_id;
    int previous_future_id;
    int reservation_id;
    int submit_started_at;

    if (projection_kind != "inventory" || protocol_type != XK_IVTY
        || !mapp(payload) || !stringp(input_token) || input_token == "")
        return 0;
    thread_status = query_owner_thread_status();
    if (!mapp(thread_status) || thread_status["enabled"] != 1
        || thread_status["stopping"] == 1)
        return 0;

    states = query_owner_read_model_states();
    state = mapp(states[projection_kind]) ? states[projection_kind] : 0;
    if (mapp(state) && state["input_token"] == input_token
        && (state["state"] == "pending" || state["state"] == "ready"))
        return 0;

    reservation = XK_PROTOCOL_CORE->xk_reserve_protocol_meta(
        this_object(), protocol_type, ([]));
    if (!mapp(reservation) || reservation["ok"] != 1)
        return 0;
    reservation_id = reservation["reservation_id"];
    previous_future_id = mapp(state) && intp(state["future_id"])
        ? state["future_id"] : 0;

    submit_started_at = perf_counter_ns();
    result = owner_call_async(this_object(), "owner_project_protocol_payload",
        copy(payload));
    future_id = mapp(result) && intp(result["future_id"])
        ? result["future_id"] : 0;
    if (!mapp(result) || result["success"] != 1 || future_id <= 0) {
        rollback_owner_read_model_future(future_id,
            "owner read model output submit failed");
        XK_PROTOCOL_CORE->xk_release_reserved_protocol(this_object(), reservation);
        record_owner_protocol_projection("owner_failed", ([
            "projection_kind": projection_kind,
            "input_token": input_token,
        ]));
        record_owner_protocol_projection_timing("submit", ([
            "projection_kind": projection_kind,
        ]), perf_counter_ns() - submit_started_at);
        return 0;
    }

    context = ([
        "projection_kind": projection_kind,
        "input_token": input_token,
        "demand_generation": query_owner_read_model_demand_generation(
            projection_kind),
        "future_id": future_id,
        "protocol_type": protocol_type,
        "reservation": copy(reservation),
        "snapshot": copy(payload),
        "read_side_worker_state": "ready",
    ]);
    contexts = query_owner_read_model_output_contexts();
    contexts[sprintf("%d", reservation_id)] = context;
    set_temp("owner/read_model_output_contexts", contexts);

    if (!gateway_session_watch_future(this_object(), reservation_id,
        future_id, OWNER_READ_MODEL_TIMEOUT_MS)) {
        contexts = query_owner_read_model_output_contexts();
        map_delete(contexts, sprintf("%d", reservation_id));
        set_temp("owner/read_model_output_contexts", contexts);
        rollback_owner_read_model_future(future_id,
            "owner read model output watch failed");
        XK_PROTOCOL_CORE->xk_release_reserved_protocol(this_object(), reservation);
        record_owner_protocol_projection("owner_failed", context);
        record_owner_protocol_projection_timing("submit", context,
            perf_counter_ns() - submit_started_at);
        return 0;
    }

    states[projection_kind] = ([
        "projection_kind": projection_kind,
        "input_token": input_token,
        "demand_generation": context["demand_generation"],
        "future_id": future_id,
        "read_side_worker_state": "ready",
        "state": "pending",
        "submitted_at": time(),
    ]);
    set_temp("owner/read_model_states", states);
    record_owner_read_model_claim(projection_kind,
        context["demand_generation"]);
    if (previous_future_id > 0 && previous_future_id != future_id) {
        owner_future_cancel(previous_future_id,
            "owner read model output superseded");
        record_owner_protocol_projection("owner_cancelled", context);
    }
    record_owner_protocol_projection_timing("submit", context,
        perf_counter_ns() - submit_started_at);
    return 1;
}

mixed owner_encode_protocol_frame(mapping task)
{
    string payload_json;

    if (!mapp(task) || !stringp(task["type"]) || strlen(task["type"]) != 4
        || !mapp(task["payload"]))
        return 0;
    payload_json = json_encode(task["payload"]);
    if (!stringp(payload_json))
        return 0;
    return XK_HEADER + task["type"] + payload_json + "\e\n";
}

int gateway_owner_future_output_completed(int reservation_id, string state)
{
    return GATEWAY_SESSION_D->record_session_output_owner_result(
        this_object(), reservation_id, state);
}

int gateway_owner_room_output_completed(int reservation_id, string state,
    int event_count, int slot_server_seq)
{
    string packet_type;
    string stream;
    int recorded;

    if (reservation_id <= 0 || event_count <= 0 || event_count > 128
        || slot_server_seq <= 0
        || (state != "completed" && state != "released"))
        return 0;
    if (state == "completed") {
        packet_type = event_count == 1 ? XK_MSGE : XK_BACH;
        stream = event_count == 1 ? "message" : "system";
        recorded = GATEWAY_SESSION_D->complete_session_output_reservation(
            this_object(), reservation_id, packet_type, stream,
            slot_server_seq);
        if (recorded)
            GATEWAY_SESSION_D->record_owner_protocol_output(state);
    } else {
        /* A failed owner projection releases the original FIFO slot.  It must
         * not promote or replace that reservation merely because its frozen
         * event count described a batch. */
        recorded = GATEWAY_SESSION_D->release_session_output_reservation_exact(
            this_object(), reservation_id, slot_server_seq);
    }
    if (recorded)
        MESSAGE_D->record_owner_room_output_completion(state, event_count);
    return recorded;
}

mapping owner_project_protocol_payload(mapping payload)
{
    mapping dynamic_defaults;
    string kind;
    mapping stable_payload;
    mapping view;
    string stable_json;
    string type;

    if (!mapp(payload))
        return ([ "ok": 0, "reason": "projection payload is required" ]);
    kind = payload["projection_kind"];
    switch (kind) {
    case "inventory":
        stable_payload = project_inventory_payload_snapshot(payload);
        dynamic_defaults = ([]);
        type = XK_IVTY;
        break;
    case "hp_dialog":
        stable_payload = mapp(payload["protocol_stable_payload"])
            ? copy(payload["protocol_stable_payload"]) : 0;
        dynamic_defaults = mapp(payload["protocol_dynamic_defaults"])
            ? copy(payload["protocol_dynamic_defaults"]) : 0;
        if (!mapp(stable_payload) || !mapp(dynamic_defaults))
            return ([ "ok": 0, "reason": "hp protocol template is required" ]);
        stable_payload["content"] = render_hp_dialog_snapshot(payload);
        type = XK_DLOG;
        break;
    case "score_dialog":
        stable_payload = mapp(payload["protocol_stable_payload"])
            ? copy(payload["protocol_stable_payload"]) : 0;
        dynamic_defaults = mapp(payload["protocol_dynamic_defaults"])
            ? copy(payload["protocol_dynamic_defaults"]) : 0;
        if (!mapp(stable_payload) || !mapp(dynamic_defaults))
            return ([ "ok": 0, "reason": "score protocol template is required" ]);
        stable_payload["content"] = render_score_dialog_snapshot(payload);
        type = XK_DLOG;
        break;
    case "map_grid":
        stable_payload = project_map_grid_payload_snapshot(payload);
        dynamic_defaults = ([
            "protocol_version": XK_PROTOCOL_VERSION_STRING,
            "projection_id": "",
            "projection_revision": 0,
            "projection_generation": 0,
            "room_id": "",
            "map_revision": 0,
        ]);
        if (!mapp(stable_payload))
            return ([ "ok": 0, "reason": "map grid snapshot is required" ]);
        type = XK_MAPS;
        break;
    default:
        return ([ "ok": 0, "reason": "unsupported projection kind" ]);
    }

    stable_json = json_encode_frozen(stable_payload);
    if (!stringp(stable_json))
        return ([ "ok": 0, "reason": "stable payload encoding failed" ]);
    view = ([
        "read_side_worker_state": stringp(stable_payload["read_side_worker_state"])
            ? stable_payload["read_side_worker_state"] : "ready",
    ]);
    if (kind == "hp_dialog" || kind == "score_dialog")
        view["content"] = stable_payload["content"];
    return ([
        "view": view,
        "preencoded": ([
            "schema": "xk/preencoded-payload-members/v1",
            "producer": "owner_project_protocol_payload/v1",
            "projection_kind": kind,
            "protocol_type": type,
            "stable_json": stable_json,
            "stable_keys": keys(stable_payload),
            "dynamic_defaults": dynamic_defaults,
        ]),
    ]);
}

private void cache_owner_protocol_projection(mapping context, mapping payload)
{
    if (!mapp(context) || !mapp(payload))
        return;
    switch (context["projection_kind"]) {
    case "hp_dialog":
        if (stringp(payload["content"]) && payload["content"] != "")
            set_temp("xk/hp_dialog_cache", ([
                "at": time(),
                "signature": context["input_token"],
                "read_side_worker_state": "ready",
                "content": payload["content"],
            ]));
        break;
    case "score_dialog":
        if (stringp(payload["content"]) && payload["content"] != "")
            set_temp("xk/score_dialog_cache", ([
                "at": time(),
                "signature": context["input_token"],
                "read_side_worker_state": "ready",
                "content": payload["content"],
            ]));
        break;
    }
}

private mapping complete_owner_read_model_future(mapping context, mapping future)
{
    mapping state;
    mapping states;
    mapping validated;
    string current_input_token;
    int publish_started_at;

    if (!mapp(context))
        return ([ "status": "missing" ]);

    states = query_owner_read_model_states();
    state = mapp(states[context["projection_kind"]])
        ? states[context["projection_kind"]] : 0;
    if (!mapp(state)
        || state["future_id"] != context["future_id"]
        || state["input_token"] != context["input_token"]) {
        record_owner_protocol_projection("owner_stale", context);
        return ([ "status": "stale" ]);
    }

    current_input_token = query_owner_read_model_input_token(
        context["projection_kind"]);
    if (current_input_token == ""
        || current_input_token != context["input_token"]) {
        state["state"] = "stale";
        state["future_id"] = 0;
        states[context["projection_kind"]] = state;
        set_temp("owner/read_model_states", states);
        record_owner_protocol_projection("owner_stale", context);
        if (context["projection_kind"] == "hp_dialog")
            schedule_owner_hp_read_model_refresh();
        else if (context["projection_kind"] == "score_dialog")
            schedule_owner_score_read_model_refresh();
        else if (context["projection_kind"] == "map_grid")
            schedule_owner_map_grid_read_model_refresh();
        return ([ "status": "stale" ]);
    }

    validated = !mapp(future) || future["state"] != "completed"
        || !mapp(future["result"]) ? 0
            : validate_owner_preencoded_result(
                context["projection_kind"], future["result"]);
    if (!mapp(validated)) {
        state["state"] = "failed";
        state["future_id"] = 0;
        state["failed_at"] = time();
        map_delete(state, "result");
        map_delete(state, "view");
        map_delete(state, "preencoded");
        states[context["projection_kind"]] = state;
        set_temp("owner/read_model_states", states);
        record_owner_protocol_projection("owner_failed", context);
        return ([ "status": "failed" ]);
    }

    record_owner_protocol_projection_publish_guard("checked");
    states = query_owner_read_model_states();
    state = mapp(states[context["projection_kind"]])
        ? states[context["projection_kind"]] : 0;
    current_input_token = query_owner_read_model_input_token(
        context["projection_kind"]);
    if (!mapp(state)
        || state["future_id"] != context["future_id"]
        || state["input_token"] != context["input_token"]
        || current_input_token == ""
        || current_input_token != context["input_token"]) {
        if (mapp(state)
            && state["future_id"] == context["future_id"]
            && state["input_token"] == context["input_token"]) {
            state["state"] = "stale";
            state["future_id"] = 0;
            states[context["projection_kind"]] = state;
            set_temp("owner/read_model_states", states);
            if (context["projection_kind"] == "hp_dialog")
                schedule_owner_hp_read_model_refresh();
            else if (context["projection_kind"] == "score_dialog")
                schedule_owner_score_read_model_refresh();
            else if (context["projection_kind"] == "map_grid")
                schedule_owner_map_grid_read_model_refresh();
        }
        record_owner_protocol_projection_publish_guard("rejected");
        record_owner_protocol_projection("owner_stale", context);
        return ([ "status": "stale" ]);
    }

    publish_started_at = perf_counter_ns();
    state["state"] = "ready";
    state["future_id"] = 0;
    state["published_at"] = time();
    state["view"] = copy(validated["view"]);
    state["preencoded"] = copy(validated["artifact"]);
    states[context["projection_kind"]] = state;
    set_temp("owner/read_model_states", states);
    cache_owner_protocol_projection(context, validated["view"]);
    record_owner_protocol_projection_publish_guard("committed");
    record_owner_protocol_projection("owner_completed", context);
    record_owner_protocol_projection_timing("publish", context,
        perf_counter_ns() - publish_started_at);
    return ([
        "status": "published",
        "view": copy(validated["view"]),
        "artifact": copy(validated["artifact"]),
    ]);
}

int owner_future_watch_completed(int context_id, mapping future)
{
    mapping context;
    mapping contexts;
    string context_key;

    context_key = sprintf("%d", context_id);
    contexts = query_owner_read_model_contexts();
    context = mapp(contexts[context_key]) ? contexts[context_key] : 0;
    map_delete(contexts, context_key);
    set_temp("owner/read_model_contexts", contexts);
    complete_owner_read_model_future(context, future);
    return 1;
}

int gateway_owner_future_cancelled(int reservation_id, int future_id,
    string reason)
{
    mapping context;
    mapping contexts;
    mapping record_context;
    mapping state;
    mapping states;
    string context_key;
    string projection_kind;
    string state_kind;

    if (reservation_id <= 0 || future_id <= 0)
        return 0;
    context_key = sprintf("%d", reservation_id);
    contexts = query_owner_read_model_output_contexts();
    context = mapp(contexts[context_key]) ? contexts[context_key] : 0;
    if (mapp(context) && context["future_id"] != future_id)
        return 0;
    if (mapp(context))
        map_delete(contexts, context_key);
    set_temp("owner/read_model_output_contexts", contexts);

    states = query_owner_read_model_states();
    projection_kind = mapp(context) && stringp(context["projection_kind"])
        ? context["projection_kind"] : "";
    state = projection_kind != "" && mapp(states[projection_kind])
        ? states[projection_kind] : 0;
    if (!mapp(state) || state["future_id"] != future_id) {
        projection_kind = "";
        foreach (state_kind, state in states) {
            if (mapp(state) && state["future_id"] == future_id) {
                projection_kind = state_kind;
                break;
            }
        }
    }
    if (projection_kind != "" && mapp(state)
        && state["future_id"] == future_id) {
        state = copy(state);
        state["state"] = "cancelled";
        state["future_id"] = 0;
        state["failed_at"] = time();
        map_delete(state, "result");
        map_delete(state, "view");
        map_delete(state, "preencoded");
        states[projection_kind] = state;
        set_temp("owner/read_model_states", states);
    }

    record_context = mapp(context) ? copy(context) : ([
        "projection_kind": projection_kind,
        "future_id": future_id,
    ]);
    record_context["reason"] = stringp(reason) && reason != ""
        ? reason : "gateway owner future cancelled";
    record_owner_protocol_projection("owner_cancelled", record_context);
    XK_PROTOCOL_CORE->xk_release_reserved_protocol_id(
        this_object(), reservation_id);
    return 1;
}

int gateway_owner_future_completed(int reservation_id, mapping future)
{
    mapping completed;
    mapping context;
    mapping contexts;
    mapping fallback;
    string context_key;
    string current_input_token;
    int fallback_started_at;

    context_key = sprintf("%d", reservation_id);
    contexts = query_owner_read_model_output_contexts();
    context = mapp(contexts[context_key]) ? contexts[context_key] : 0;
    map_delete(contexts, context_key);
    set_temp("owner/read_model_output_contexts", contexts);
    if (!mapp(context))
        return XK_PROTOCOL_CORE->xk_release_reserved_protocol_id(
            this_object(), reservation_id);

    completed = complete_owner_read_model_future(context, future);
    if (completed["status"] == "published")
        if (XK_PROTOCOL_CORE->xk_fill_reserved_preencoded_artifact(
            this_object(), context["protocol_type"], completed["artifact"],
            context["reservation"]))
            return 1;

    current_input_token = query_owner_read_model_input_token(
        context["projection_kind"]);
    if ((completed["status"] != "failed"
            && completed["status"] != "published")
        || current_input_token != context["input_token"]) {
        XK_PROTOCOL_CORE->xk_release_reserved_protocol(
            this_object(), context["reservation"]);
        return 1;
    }

    fallback_started_at = perf_counter_ns();
    fallback = project_inventory_payload_snapshot(context["snapshot"]);
    if (!mapp(fallback)) {
        XK_PROTOCOL_CORE->xk_release_reserved_protocol(
            this_object(), context["reservation"]);
        return 1;
    }
    fallback["read_side_worker"] = 0;
    fallback["read_side_worker_state"] = "main_thread";
    record_owner_protocol_sync_fallback(context["projection_kind"],
        completed["status"] == "published"
            ? "owner preencoded artifact fill failed"
            : "owner read model output failed");
    record_owner_protocol_projection_timing("sync_fallback", context,
        perf_counter_ns() - fallback_started_at);
    call_other("/kernel/simul_efun_room_entities_xk",
        "xk_cache_inventory_payload", this_object(), fallback);
    return XK_PROTOCOL_CORE->xk_fill_reserved_protocol(
        this_object(), context["protocol_type"], fallback,
        context["reservation"]);
}

void refresh_owner_hp_read_model()
{
    mapping dialog_template;
    mapping snapshot;
    string current_input_token;
    string input_token;
    string worker_state;
    int refresh_started_at;
    int snapshot_started_at;

    refresh_started_at = perf_counter_ns();
    delete_temp("owner/hp_read_model_refresh_handle");
    if (!interactive(this_object())) {
        record_owner_protocol_projection_timing("refresh_total", ([
            "projection_kind": "hp_dialog",
        ]), perf_counter_ns() - refresh_started_at);
        return;
    }

    snapshot_started_at = perf_counter_ns();
    worker_state = "ready";
    input_token = query_owner_hp_read_model_live_token();
    snapshot = CLIENT_PROJECTION_D->query_hp_projection_snapshot(
        this_object(), worker_state);
    current_input_token = query_owner_hp_read_model_live_token();
    if (input_token == "" || current_input_token != input_token) {
        record_owner_protocol_projection_timing("snapshot", snapshot,
            perf_counter_ns() - snapshot_started_at);
        schedule_owner_hp_read_model_refresh(1);
        record_owner_protocol_projection_timing("refresh_total", ([
            "projection_kind": "hp_dialog",
        ]), perf_counter_ns() - refresh_started_at);
        return;
    }
    snapshot["payload_key"] = "xk/hp-dialog-owner-projection/v1";
    snapshot["projection_kind"] = "hp_dialog";
    snapshot["signature"] = input_token;
    dialog_template = CLIENT_PROJECTION_D->query_hp_dialog_preencoded_template();
    if (!mapp(dialog_template) || !mapp(dialog_template["stable_payload"])
        || !mapp(dialog_template["dynamic_defaults"])) {
        record_owner_protocol_projection_timing("refresh_total", ([
            "projection_kind": "hp_dialog",
        ]), perf_counter_ns() - refresh_started_at);
        return;
    }
    snapshot["protocol_stable_payload"] = copy(dialog_template["stable_payload"]);
    snapshot["protocol_dynamic_defaults"] = copy(dialog_template["dynamic_defaults"]);
    record_owner_protocol_projection_timing("snapshot", snapshot,
        perf_counter_ns() - snapshot_started_at);
    publish_read_model_artifact("hp_dialog", snapshot, input_token);
    record_owner_protocol_projection_timing("refresh_total", ([
        "projection_kind": "hp_dialog",
    ]), perf_counter_ns() - refresh_started_at);
}

private string owner_read_model_refresh_handle_key(string projection_kind)
{
    if (projection_kind == "hp_dialog")
        return "owner/hp_read_model_refresh_handle";
    if (projection_kind == "score_dialog")
        return "owner/score_read_model_refresh_handle";
    if (projection_kind == "map_grid")
        return "owner/map_grid_read_model_refresh_handle";
    return "";
}

private void schedule_owner_read_model_background_callout(
    string projection_kind, string function_name)
{
    string handle_key;
    mixed handle;

    handle_key = owner_read_model_refresh_handle_key(projection_kind);
    if (handle_key == "" || !stringp(function_name) || function_name == "")
        return;
    handle = query_temp(handle_key);
    if (intp(handle) && handle > 0)
        return;
    handle = call_out_walltime_background(function_name, 0);
    if (intp(handle) && handle > 0)
        set_temp(handle_key, handle);
}

private void cancel_owner_read_model_refresh_callout(string projection_kind)
{
    string handle_key;
    mixed handle;

    handle_key = owner_read_model_refresh_handle_key(projection_kind);
    if (handle_key == "")
        return;
    handle = query_temp(handle_key);
    if (intp(handle) && handle > 0)
        remove_call_out(handle);
    delete_temp(handle_key);
}

varargs void schedule_owner_hp_read_model_refresh(int force_refresh)
{
    if (!interactive(this_object()))
        return;
    if (!claim_owner_read_model_refresh("hp_dialog", force_refresh))
        return;
    schedule_owner_read_model_background_callout("hp_dialog",
        "refresh_owner_hp_read_model");
}

private int complete_owner_score_refresh_timing(mixed refresh_started_at)
{
    mixed refresh_finished_at;
    int elapsed_ns;

    refresh_finished_at = perf_counter_ns();
    elapsed_ns = !intp(refresh_started_at) || !intp(refresh_finished_at)
        || refresh_started_at < 0 || refresh_finished_at < refresh_started_at
            ? -1 : refresh_finished_at - refresh_started_at;
    record_owner_protocol_projection_timing("refresh_total", ([
        "projection_kind": "score_dialog",
    ]), elapsed_ns);
    return elapsed_ns;
}

private int refresh_owner_score_read_model_impl()
{
    mapping dialog_template;
    mapping snapshot;
    string current_input_token;
    string input_token;
    int attempt;
    mixed refresh_started_at;
    int snapshot_started_at;

    refresh_started_at = perf_counter_ns();
    delete_temp("owner/score_read_model_refresh_handle");
    if (!interactive(this_object()))
        return complete_owner_score_refresh_timing(refresh_started_at);
    for (attempt = 0; attempt < SCORE_READ_MODEL_REFRESH_MAX_ATTEMPTS; attempt++) {
        snapshot_started_at = perf_counter_ns();
        snapshot = CLIENT_PROJECTION_D->query_score_projection_snapshot(
            this_object());
        input_token = CLIENT_PROJECTION_D->query_score_projection_live_token_from_snapshot(
            this_object(), snapshot);
        if (!mapp(snapshot) || !stringp(input_token) || input_token == "")
            return complete_owner_score_refresh_timing(refresh_started_at);
        snapshot["payload_key"] = "xk/score-dialog-owner-projection/v1";
        snapshot["projection_kind"] = "score_dialog";
        snapshot["signature"] = input_token;
        snapshot["read_side_worker_state"] = "ready";
        dialog_template = CLIENT_PROJECTION_D->query_score_dialog_preencoded_template();
        if (!mapp(dialog_template) || !mapp(dialog_template["stable_payload"])
            || !mapp(dialog_template["dynamic_defaults"]))
            return complete_owner_score_refresh_timing(refresh_started_at);
        snapshot["protocol_stable_payload"] =
            copy(dialog_template["stable_payload"]);
        snapshot["protocol_dynamic_defaults"] =
            copy(dialog_template["dynamic_defaults"]);
        record_owner_protocol_projection_timing("snapshot", snapshot,
            perf_counter_ns() - snapshot_started_at);
        if (publish_read_model_artifact("score_dialog", snapshot, input_token))
            return complete_owner_score_refresh_timing(refresh_started_at);

        current_input_token = query_owner_read_model_input_token("score_dialog");
        if (current_input_token == "" || current_input_token == input_token)
            return complete_owner_score_refresh_timing(refresh_started_at);
    }
    return complete_owner_score_refresh_timing(refresh_started_at);
}

void refresh_owner_score_read_model()
{
    refresh_owner_score_read_model_impl();
}

int refresh_owner_score_read_model_with_elapsed()
{
    return refresh_owner_score_read_model_impl();
}

varargs void schedule_owner_score_read_model_refresh(int force_refresh)
{
    if (!interactive(this_object()))
        return;
    if (!claim_owner_read_model_refresh("score_dialog", force_refresh))
        return;
    schedule_owner_read_model_background_callout("score_dialog",
        "refresh_owner_score_read_model");
}

void refresh_owner_map_grid_read_model()
{
    mapping snapshot;
    string input_token;
    int refresh_started_at;
    int snapshot_started_at;

    refresh_started_at = perf_counter_ns();
    delete_temp("owner/map_grid_read_model_refresh_handle");
    if (!interactive(this_object())) {
        record_owner_protocol_projection_timing("refresh_total", ([
            "projection_kind": "map_grid",
        ]), perf_counter_ns() - refresh_started_at);
        return;
    }
    snapshot_started_at = perf_counter_ns();
    snapshot = call_other(ROOM_MAPREAD_XK,
        "xk_query_map_grid_read_model_snapshot", this_object());
    if (!mapp(snapshot) || !stringp(snapshot["input_token"])
        || snapshot["input_token"] == "") {
        record_owner_protocol_projection_timing("refresh_total", ([
            "projection_kind": "map_grid",
        ]), perf_counter_ns() - refresh_started_at);
        return;
    }
    input_token = snapshot["input_token"];
    snapshot["projection_kind"] = "map_grid";
    snapshot["read_side_worker"] = 1;
    snapshot["read_side_worker_state"] = "ready_cached";
    record_owner_protocol_projection_timing("snapshot", snapshot,
        perf_counter_ns() - snapshot_started_at);
    publish_read_model_artifact("map_grid", snapshot, input_token);
    record_owner_protocol_projection_timing("refresh_total", ([
        "projection_kind": "map_grid",
    ]), perf_counter_ns() - refresh_started_at);
}

varargs void schedule_owner_map_grid_read_model_refresh(int force_refresh)
{
    if (!interactive(this_object()))
        return;
    if (!claim_owner_read_model_refresh("map_grid", force_refresh))
        return;
    schedule_owner_read_model_background_callout("map_grid",
        "refresh_owner_map_grid_read_model");
}

void gateway_disconnected(string reason_code, string reason_text)
{
    mapping context;
    mapping contexts;

    cancel_owner_read_model_refresh_callout("hp_dialog");
    cancel_owner_read_model_refresh_callout("score_dialog");
    cancel_owner_read_model_refresh_callout("map_grid");
    call_other(ROOM_ENTITIES_XK, "cancel_inventory_read_model_refresh",
        this_object());

    contexts = query_owner_read_model_contexts();
    foreach (context in values(contexts)) {
        if (mapp(context) && intp(context["future_id"])
            && context["future_id"] > 0) {
            owner_future_cancel(context["future_id"],
                "gateway disconnected");
            record_owner_protocol_projection("owner_cancelled", context);
        }
    }
    delete_temp("owner/read_model_contexts");
    delete_temp("owner/read_model_output_contexts");
    delete_temp("owner/read_model_states");
    delete_temp("owner/read_model_demand_generations");
    delete_temp("owner/read_model_claimed_generations");
}

private void touch_gateway_session_activity(string session_id)
{
    mixed last_touch;
    int now;

    if (!stringp(session_id) || session_id == "")
        return;

    now = time();
    last_touch = query_temp("gateway/activity_touch");
    if (mapp(last_touch) && stringp(last_touch["session_id"])
        && last_touch["session_id"] == session_id
        && intp(last_touch["at"]) && now >= last_touch["at"]
        && now - last_touch["at"] < GATEWAY_SESSION_TOUCH_INTERVAL_SECONDS)
        return;

    GATEWAY_SESSION_D->touch_session(session_id);
    set_temp("gateway/activity_touch", ([
        "session_id": session_id,
        "at": now,
    ]));
}

int health_check()
{
    return function_exists("gateway_receive", this_object())
        && function_exists("query_player_session_owner_route", this_object())
        ? 1 : 0;
}

mapping query_player_session_owner_route()
{
    mapping route;
    string session_id;
    string player_id;
    string route_key;
    mixed err;

    session_id = GATEWAY_SESSION_D->query_object_session_id(this_object());
    player_id = query_temp("player/account_id") || geteuid(this_object()) || file_name(this_object());
    route_key = stringp(session_id) && session_id != "" ? session_id : player_id;
    err = catch(route = OWNER_RUNTIME_D->query_service_owner_route("player_session", route_key));
    if (err || !mapp(route) || route["ok"] != 1) {
        return ([
            "ok": 0,
            "service": "player_session",
            "key": route_key,
            "session_id": session_id,
            "player_id": player_id,
            "failure_reason": err ? sprintf("owner route query failed: %O", err)
                : (mapp(route) ? route["failure_reason"] : "owner route unavailable"),
            "readiness": mapp(route) ? route["readiness"] : ([ "ok": 0 ]),
            "dependencies": mapp(route) ? route["dependencies"] : ([ ]),
            "direct_cross_owner_write": 0,
        ]);
    }

    route["session_id"] = session_id;
    route["player_id"] = player_id;
    route["owner_output_boundary"] = 1;
    route["direct_cross_owner_write"] = 0;
    return route;
}

mapping refresh_player_session_owner_route(string reason)
{
    mapping route;

    route = query_player_session_owner_route();
    set_temp("owner/player_session_route", route);
    set_temp("owner/player_session_route_reason", stringp(reason) ? reason : "");
    return route;
}

private int ensure_player_session_owner_route(string reason, string gateway_session_id)
{
    mixed route;

    route = query_temp("owner/player_session_route");
    if (mapp(route) && route["ok"] == 1 && stringp(route["session_id"])
        && route["session_id"] == gateway_session_id)
        return 1;

    route = refresh_player_session_owner_route(reason);
    return mapp(route) && route["ok"] == 1 ? 1 : 0;
}

private int valid_command_request_id(string value)
{
    int index;

    if (!stringp(value) || value == "" || strlen(value) > 96)
        return 0;
    for (index = 0; index < strlen(value); index++)
        if (value[index] < 33 || value[index] > 126)
            return 0;
    return 1;
}

private int valid_command_payload(string value)
{
    int index;

    if (!stringp(value) || value == "" || strlen(value) > MAX_COMMAND_LENGTH)
        return 0;
    if (strsrch(value, "\n") != -1)
        return 0;
    if (strsrch(value, "\r") != -1)
        return 0;
    for (index = 0; index < strlen(value); index++)
        if (value[index] == 0)
            return 0;
    return 1;
}

private int command_envelope_candidate(string value)
{
    string trimmed;

    if (!stringp(value))
        return 0;
    trimmed = trim(value);
    if (strlen(trimmed) < 1 || trimmed[0] != '{')
        return 0;
    return 1;
}

private int command_envelope_mapping_marker(mapping value)
{
    if (!mapp(value))
        return 0;
    return !undefinedp(value["schema"])
        || !undefinedp(value["command_request_id"]);
}

private int json_skip_space(string raw, int pos, int end)
{
    while (pos < end && (raw[pos] == ' ' || raw[pos] == '\t'
        || raw[pos] == '\r' || raw[pos] == '\n'))
        pos++;
    return pos;
}

private int json_hex_digit(int value)
{
    return (value >= '0' && value <= '9')
        || (value >= 'a' && value <= 'f')
        || (value >= 'A' && value <= 'F');
}

private int json_skip_string(string raw, int pos, int end)
{
    int escaped;
    int index;

    if (pos >= end || raw[pos] != '"')
        return -1;
    for (index = pos + 1; index < end; index++) {
        if (escaped) {
            if (raw[index] == 'u') {
                if (index + 4 >= end
                    || !json_hex_digit(raw[index + 1])
                    || !json_hex_digit(raw[index + 2])
                    || !json_hex_digit(raw[index + 3])
                    || !json_hex_digit(raw[index + 4]))
                    return -1;
                index += 4;
            } else if (strsrch("\\\"/bfnrt", raw[index..index]) == -1) {
                return -1;
            }
            escaped = 0;
            continue;
        }
        if (raw[index] == '\\') {
            escaped = 1;
            continue;
        }
        if (raw[index] < 32)
            return -1;
        if (raw[index] == '"')
            return index + 1;
    }
    return -1;
}

private int json_skip_value(string raw, int pos, int end, int depth)
{
    int next;
    int value_start;

    if (depth > 16)
        return -1;
    pos = json_skip_space(raw, pos, end);
    if (pos >= end)
        return -1;
    if (raw[pos] == '"')
        return json_skip_string(raw, pos, end);
    if (raw[pos] == '{') {
        pos = json_skip_space(raw, pos + 1, end);
        if (pos < end && raw[pos] == '}')
            return pos + 1;
        while (pos < end) {
            if (raw[pos] != '"')
                return -1;
            next = json_skip_string(raw, pos, end);
            if (next < 0)
                return -1;
            pos = json_skip_space(raw, next, end);
            if (pos >= end || raw[pos] != ':')
                return -1;
            next = json_skip_value(raw, pos + 1, end, depth + 1);
            if (next < 0)
                return -1;
            pos = json_skip_space(raw, next, end);
            if (pos < end && raw[pos] == '}')
                return pos + 1;
            if (pos >= end || raw[pos] != ',')
                return -1;
            pos = json_skip_space(raw, pos + 1, end);
        }
        return -1;
    }
    if (raw[pos] == '[') {
        pos = json_skip_space(raw, pos + 1, end);
        if (pos < end && raw[pos] == ']')
            return pos + 1;
        while (pos < end) {
            next = json_skip_value(raw, pos, end, depth + 1);
            if (next < 0)
                return -1;
            pos = json_skip_space(raw, next, end);
            if (pos < end && raw[pos] == ']')
                return pos + 1;
            if (pos >= end || raw[pos] != ',')
                return -1;
            pos = json_skip_space(raw, pos + 1, end);
        }
        return -1;
    }

    value_start = pos;
    while (pos < end && raw[pos] != ',' && raw[pos] != '}' && raw[pos] != ']')
        pos++;
    while (pos > value_start && (raw[pos - 1] == ' ' || raw[pos - 1] == '\t'
        || raw[pos - 1] == '\r' || raw[pos - 1] == '\n'))
        pos--;
    return pos > value_start ? pos : -1;
}

private int valid_command_envelope_json(string raw)
{
    mapping seen;
    mixed decoded_key;
    mixed err;
    int pos;
    int next;
    int end;
    string key_raw;

    if (!stringp(raw))
        return 0;
    end = strlen(raw);
    pos = json_skip_space(raw, 0, end);
    if (pos >= end || raw[pos] != '{')
        return 0;
    pos = json_skip_space(raw, pos + 1, end);
    seen = ([]);
    if (pos < end && raw[pos] == '}')
        return 0;
    while (pos < end) {
        int key_start;

        pos = json_skip_space(raw, pos, end);
        key_start = pos;
        next = json_skip_string(raw, pos, end);
        if (next < 0)
            return 0;
        key_raw = raw[key_start..next - 1];
        err = catch(decoded_key = json_decode(key_raw));
        if (err || !stringp(decoded_key) || !undefinedp(seen[decoded_key]))
            return 0;
        seen[decoded_key] = 1;
        pos = json_skip_space(raw, next, end);
        if (pos >= end || raw[pos] != ':')
            return 0;
        next = json_skip_value(raw, pos + 1, end, 0);
        if (next < 0)
            return 0;
        pos = json_skip_space(raw, next, end);
        if (pos < end && raw[pos] == '}')
            return pos + 1 == end;
        if (pos >= end || raw[pos] != ',')
            return 0;
        pos = json_skip_space(raw, pos + 1, end);
    }
    return 0;
}

private mapping decode_command_envelope(mixed value)
{
    mapping envelope;
    mixed decoded;
    mixed err;
    string *envelope_keys;
    string raw;
    int index;
    int known_key;

    if (stringp(value)) {
        raw = trim(value);
        if (!command_envelope_candidate(raw)
            || !valid_command_envelope_json(raw))
            return 0;
        err = catch(decoded = json_decode(raw));
        if (err || !mapp(decoded))
            return 0;
        envelope = decoded;
    } else if (mapp(value)) {
        if (!command_envelope_mapping_marker(value))
            return 0;
        envelope = value;
    } else
        return 0;

    envelope_keys = keys(envelope);
    if (!arrayp(envelope_keys) || sizeof(envelope_keys) != 3)
        return 0;
    for (index = 0; index < sizeof(envelope_keys); index++) {
        known_key = envelope_keys[index] == "schema"
            || envelope_keys[index] == "command_request_id"
            || envelope_keys[index] == "payload";
        if (!known_key)
            return 0;
    }
    if (envelope["schema"] != "xk.input.v1"
        || !valid_command_request_id(envelope["command_request_id"])
        || !valid_command_payload(envelope["payload"]))
        return 0;
    if (command_envelope_candidate(envelope["payload"]))
        return 0;

    return ([
        "command_request_id": envelope["command_request_id"],
        "payload": envelope["payload"],
    ]);
}

private string reject_gateway_receive(mixed previous_response_context,
    mixed previous_command_scope, string result)
{
    if (stringp(previous_response_context)
        && previous_response_context != "")
        set_temp("gateway/command_request_id", previous_response_context);
    else
        delete_temp("gateway/command_request_id");
    if (previous_command_scope)
        set_temp("gateway/command_context_active", previous_command_scope);
    else
        delete_temp("gateway/command_context_active");
    return stringp(result) ? result : "";
}

string gateway_receive(mixed data)
{
    string input;
    string command_request_id;
    string gateway_session_id;
    string result;
    mapping command_envelope;
    mixed nested_data;
    mixed process_error;
    mixed previous_response_context;
    mixed previous_command_scope;
    int input_decode_elapsed_ns;
    int phase_started_at;
    int process_input_elapsed_ns;
    int receive_started_at;
    int route_ensure_elapsed_ns;
    int session_lookup_elapsed_ns;
    int session_touch_elapsed_ns;
    int started_at;

    receive_started_at = perf_counter_ns();
    command_request_id = "";
    previous_response_context = query_temp("gateway/command_request_id");
    previous_command_scope = query_temp("gateway/command_context_active");
    phase_started_at = receive_started_at;
    gateway_session_id = query_temp("gateway/session_id");
    if (!stringp(gateway_session_id) || gateway_session_id == "")
        gateway_session_id = GATEWAY_SESSION_D->query_object_session_id(this_object());
    session_lookup_elapsed_ns = perf_counter_ns() - phase_started_at;
    phase_started_at = perf_counter_ns();
    if (stringp(gateway_session_id) && gateway_session_id != "")
        touch_gateway_session_activity(gateway_session_id);
    session_touch_elapsed_ns = perf_counter_ns() - phase_started_at;
    phase_started_at = perf_counter_ns();
    if (!ensure_player_session_owner_route("gateway_receive", gateway_session_id)) {
        route_ensure_elapsed_ns = perf_counter_ns() - phase_started_at;
        GATEWAY_TELEMETRY_D->finish_gateway_receive_measurement(this_object(),
            gateway_session_id, "", perf_counter_ns() - receive_started_at,
            session_lookup_elapsed_ns, session_touch_elapsed_ns,
            route_ensure_elapsed_ns, 0, 0);
        return reject_gateway_receive(previous_response_context,
            previous_command_scope, "owner route unavailable");
    }
    route_ensure_elapsed_ns = perf_counter_ns() - phase_started_at;
    phase_started_at = perf_counter_ns();

    if (stringp(data))
        input = data;
    else if (mapp(data) && command_envelope_mapping_marker(data)) {
        command_envelope = decode_command_envelope(data);
        if (!mapp(command_envelope))
            return reject_gateway_receive(previous_response_context,
                previous_command_scope, "");
        input = command_envelope["payload"];
        command_request_id = command_envelope["command_request_id"];
    } else if (mapp(data) && stringp(data["payload"]))
        input = data["payload"];
    else if (mapp(data) && stringp(data["data"]))
        input = data["data"];
    else if (mapp(data) && mapp(data["data"])) {
        nested_data = data["data"];
        if (command_envelope_mapping_marker(nested_data)) {
            command_envelope = decode_command_envelope(nested_data);
            if (!mapp(command_envelope))
                return reject_gateway_receive(previous_response_context,
                    previous_command_scope, "");
            input = command_envelope["payload"];
            command_request_id = command_envelope["command_request_id"];
        } else if (stringp(nested_data["payload"]))
            input = nested_data["payload"];
        else if (stringp(nested_data["data"]))
            input = nested_data["data"];
        else
            return reject_gateway_receive(previous_response_context,
                previous_command_scope, "");
    } else
        return reject_gateway_receive(previous_response_context,
            previous_command_scope, "");

    if (!stringp(input))
        return reject_gateway_receive(previous_response_context,
            previous_command_scope, "");
    if (command_request_id == "" && command_envelope_candidate(input)) {
        command_envelope = decode_command_envelope(input);
        if (!mapp(command_envelope))
            return reject_gateway_receive(previous_response_context,
                previous_command_scope, "");
        input = command_envelope["payload"];
        command_request_id = command_envelope["command_request_id"];
    }
    input_decode_elapsed_ns = perf_counter_ns() - phase_started_at;

    if (stringp(command_request_id) && command_request_id != "")
        set_temp("gateway/command_request_id", command_request_id);
    else
        delete_temp("gateway/command_request_id");
    /* Generic protocol producers may capture the current request only while
     * this synchronous command scope is active.  Deferred work therefore
     * cannot read a later command's temp value and mis-tag its own output. */
    set_temp("gateway/command_context_active", 1);
    started_at = perf_counter_ns();
    process_error = catch(result = process_input(input));
    process_input_elapsed_ns = perf_counter_ns() - started_at;
    if (stringp(previous_response_context)
        && previous_response_context != "")
        set_temp("gateway/command_request_id", previous_response_context);
    else
        delete_temp("gateway/command_request_id");
    if (previous_command_scope)
        set_temp("gateway/command_context_active", previous_command_scope);
    else
        delete_temp("gateway/command_context_active");
    GATEWAY_TELEMETRY_D->finish_gateway_receive_measurement(this_object(),
        gateway_session_id, input, perf_counter_ns() - receive_started_at,
        session_lookup_elapsed_ns, session_touch_elapsed_ns,
        route_ensure_elapsed_ns, input_decode_elapsed_ns,
        process_input_elapsed_ns);
    return process_error ? "" : result;
}

void set_account_id(string id)
{
    set_temp("player/account_id", id);
}

string query_account_id()
{
    return query_temp("player/account_id") || "";
}

void set_net_dead(int flag)
{
    set_temp("net_dead", flag);
}

int is_net_dead()
{
    return query_temp("net_dead") || 0;
}

void reconnect()
{
    set_temp("net_dead", 0);
}

void touch_command_time()
{
    set_temp("player/last_command_time", time());
}

int query_last_command_time()
{
    return query_temp("player/last_command_time") || 0;
}

int is_idle_timeout(int timeout)
{
    return (time() - query_last_command_time()) > timeout;
}

void lock_account(string reason)
{
    set_temp("player/locked", 1);
}

void unlock_account()
{
    set_temp("player/locked", 0);
}

int is_locked()
{
    return query_temp("player/locked") || 0;
}

void add_login_fail()
{
    int fail_count;

    fail_count = (query_temp("player/login_fail_count") || 0) + 1;
    set_temp("player/login_fail_count", fail_count);
    if (fail_count >= 5) {
        lock_account("连续5次登录失败");
    }
}

void reset_login_fail()
{
    set_temp("player/login_fail_count", 0);
}

void setup_player_context(string my_id)
{
    if (!geteuid()) {
        seteuid(my_id);
    }

    set_temp("player/login_time", time());
    set_temp("player/last_command_time", time());
    refresh_player_session_owner_route("setup_player_context");
}
