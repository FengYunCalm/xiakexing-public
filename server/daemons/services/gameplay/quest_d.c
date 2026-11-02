/**
 * @file    quest_d.c
 * @brief   任务服务真源



 *
 * @par Description:
 * 当前任务系统统一真源，负责任务定义、玩家任务状态与奖励发放。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/constants.h>
#include <system/services.h>
#include <system/event_names.h>
#include <game/quest.h>

inherit "/lib/framework/daemon";

#define QUEST_CATALOG_HELPER    "/daemons/services/gameplay/quest/quest_catalog"
#define QUEST_ESCORT_RUNTIME_HELPER "/daemons/services/gameplay/quest/quest_escort_runtime"
inherit "/daemons/services/gameplay/quest/quest_lifecycle";
inherit "/daemons/services/gameplay/quest/quest_event_router";
#define QUEST_PROGRESS_HELPER   "/daemons/services/gameplay/quest/quest_progress_write"
#define QUEST_PROGRESS_D        "/daemons/services/gameplay/quest/quest_progress_d"
#define QUEST_STATE_STORE_HELPER "/daemons/services/gameplay/quest/quest_state_store"
#define QUEST_QUERY_HELPER      "/daemons/services/gameplay/quest/quest_query"
#define QUEST_STEP_MACHINE_HELPER "/daemons/services/gameplay/quest/quest_step_machine"
#define QUEST_VALIDATE_HELPER   "/daemons/services/gameplay/quest/quest_validate"
#define QUEST_TRANSITION_D      "/daemons/services/gameplay/quest/quest_transition_d"
#define QUEST_SETTLEMENT_D      "/daemons/services/gameplay/quest/quest_settlement_d"
inherit "/daemons/services/gameplay/quest/quest_render";

nosave mapping _quest_definitions;
mapping _player_quests;
nosave mapping _public_quest_runtime;
nosave private int quest_definition_generation = 1;
/* L-140：损坏存档进入待恢复状态；保存被拒绝，直到人工确认重建。 */
nosave int _quest_restore_failed;
nosave mapping _last_quest_save_phase;
/* L-144：mutation 批次超过该秒数未推进视为异常残留（异常跳过 finish），
 * 在下次 begin 或登录恢复时按失败回滚并释放，不允许 depth 永久 > 0。 */
#define QUEST_MUTATION_STALE_SECONDS 300

mapping query_health_snapshot();

mapping query_quest_definitions()
{
    return mapp(_quest_definitions) ? _quest_definitions : ([]);
}

private mapping copy_string_array_mapping(mapping source)
{
    mapping result;
    string key;

    result = ([]);
    if (!mapp(source))
        return result;

    foreach (key in keys(source)) {
        if (!stringp(key) || key == "" || !arrayp(source[key]))
            continue;
        result[key] = source[key] + ({ });
    }
    return result;
}

mapping query_visible_quest_definitions(object player)
{
    mapping result;
    mapping definition;
    string quest_id;

    result = ([]);
    foreach (quest_id, definition in (_quest_definitions || ([]))) {
        if (mapp(definition)
            && CONTENT_MODE_POLICY_D->is_content_visible(player, "quest", definition))
            result[quest_id] = copy(definition);
    }
    return result;
}

/*
 * Runtime callers must resolve a definition through the actor's active public
 * content profile and character scope. The unrestricted catalog API remains available
 * for admin and blueprint validation, but it must never decide player progress
 * or rewards.
 */
mapping query_visible_quest_definition(object player, string quest_id)
{
    mapping definition;

    if (!objectp(player)
        || !stringp(quest_id) || quest_id == "")
        return 0;
    definition = _quest_definitions[quest_id];
    if (!mapp(definition)
        || !CONTENT_MODE_POLICY_D->is_content_visible(player, "quest",
            definition))
        return 0;
    return copy(definition);
}

mapping query_visible_quests_by_type(object player, string type)
{
    mapping definitions;
    mapping result;
    mapping definition;
    string quest_id;

    result = ([]);
    if (!stringp(type) || type == "")
        return result;
    definitions = query_visible_quest_definitions(player);
    foreach (quest_id, definition in definitions) {
        if (mapp(definition) && definition["type"] == type)
            result[quest_id] = definition;
    }
    return result;
}

mapping query_visible_quests_by_provider(object player, string provider_id)
{
    mapping definitions;
    mapping result;
    mapping definition;
    string quest_id;

    result = ([]);
    if (!stringp(provider_id) || provider_id == "")
        return result;
    definitions = query_visible_quest_definitions(player);
    foreach (quest_id, definition in definitions) {
        if (mapp(definition) && definition["provider"] == provider_id)
            result[quest_id] = definition;
    }
    return result;
}

private string get_quest_save_file();
private void restore_quest_data();
int save_quest_data();
int purge_player_quest_data(string player_id);
mapping ensure_player_data(object player);
int player_has_quest_event_state(object player);
mapping query_player_quest_vars(object player);
mixed query_player_quest_var(object player, string key);
int set_player_quest_var(object player, string key, mixed value);
int query_quest_definition_generation();
int query_player_map_projection_generation(object player);
void mark_player_map_projection_changed(object player);
int record_item_inspection(object player, object item);
int record_look_progress(object player, mixed target);
varargs int record_action_progress(object player, string action, mixed target, string outcome);
int attempt_scene_action(object player, string action_id, mixed target);
int record_consume_progress(object player, mixed item_info, string action);
private void load_default_quest_definitions();
void reload_runtime_blueprints();
private mapping query_player_quest_record(object player, string quest_id);
private int is_map_marker_provider_step_actionable(mapping quest_info, string provider_id);
private int is_interactive_step_type(string step_type);
string *query_object_ids(object target);
void refresh_live_projection_state(object player);
mapping capture_quest_sync_snapshot(mapping data);
string *collect_changed_quest_ids(mapping before_snapshot, mapping data);
void emit_quest_sync_for_changes(object player, string *quest_ids,
    mapping response_context);
void queue_quest_event(object player, string event_name, string quest_id);
int begin_quest_mutation(object player);
int is_stale_quest_mutation(mapping state);
mapping release_stale_quest_mutation(object player);
int finish_quest_mutation(object player, int succeeded, int refresh_world);
private void restore_quest_mutation_snapshot(object player, mapping state);
private void record_quest_reward_repair_pending(object player,
    string *quest_ids, mapping data);
mapping repair_quest_reward_pending(object player);
private int quest_mutation_affects_provider_actions(object player,
    mapping before_snapshot, string *quest_ids);
private string *emit_quest_mutation_feedback(object player, mapping before_snapshot,
    string *quest_ids, mixed *feedback, mapping response_context);
private void auto_complete_pending_quests(object player, mapping before_snapshot,
    mapping data, string *quest_ids);
private string *collect_pending_turn_in_ids(mapping data);
private mixed *take_pending_reward_feedback(object player);
private void flush_pending_reward_feedback(object player, mixed *feedback,
    string *reported_ids, mapping response_context);
private void flush_pending_quest_events(object player, mapping state);
private void emit_quest_event(object player, string event_name, string quest_id);
private mapping query_public_quest_runtime_entry(object player);
private int persist_public_quest_runtime(object player, mapping data,
    int expected_revision);
void invalidate_public_quest_runtime(object player);
mapping flush_public_quest_state(object player, string checkpoint_id);
mapping get_quest_definition(string quest_id);
mapping query_visible_quest_definition(object player, string quest_id);
mapping query_player_quest_runtime(object player, string quest_id);
private int can_turn_in_quest(object player, string quest_id);
int record_dialog_progress(object player, object target, string event_name, string topic);
int record_escort_progress(object player, object old_env, object new_env);
int record_move_progress(object player, object old_env, object new_env);
mapping release_player_escort_targets(object player);
int record_give_progress(object player, object target, object item, int count);
string *query_matching_give_quests(object player, object target, object item, int count);
int record_kill_progress(object player, string target_id, int count);
int record_collect_progress(object player, string item_id, int count);
int record_item_acquired(object player, string item_id, int count);
mapping get_quests_by_provider(string provider_id);
mapping query_quest_validation_report();
string render_quest_validation_report();
int is_quest_visible_for_player(object player, string quest_id);
int can_accept_quest(object player, string quest_id);
int turn_in_quest(object player, string quest_id);
string render_player_quests_text(object player, string status);
string render_available_quests_text(string type, object player);
string render_quest_info_text(object player, string quest_id);
string render_provider_quests_text(string provider_id, object player);
string *query_matching_dialog_quests(object player, object target, string event_name, string topic);

private string get_quest_save_file()
{
    return DATA_DIR + "runtime/quest_d";
}

private mapping copy_quest_runtime_data(mapping data)
{
    if (!mapp(data))
        return ([]);
    return restore_variable(save_variable(data));
}

private void restore_quest_data()
{
    string file;

    file = get_quest_save_file();
    if (file_size(file + ".o") <= 0) {
        _player_quests = ([]);
        return;
    }

    if (catch(restore_object(file))) {
        /* L-140：损坏存档不得以空账本覆盖唯一存档。进入待恢复状态：
         * 保留磁盘文件与可定位日志，拒绝一切保存，直到管理员显式调用
         * rebuild_quest_ledger_from_corrupt() 转存损坏副本后重建。 */
        log_file("system/quest", sprintf(
            "[%s] restore quest data failed; ledger frozen, save refused: %s\n",
            ctime(time()), file));
        if (!mapp(_player_quests))
            _player_quests = ([]);
        _quest_restore_failed = 1;
        return;
    }

    if (!mapp(_player_quests))
        _player_quests = ([]);
    _quest_restore_failed = 0;
}

/* L-140：人工确认的重建路径——先转存损坏副本，再清空并解除保存封锁。
 * 未处于待恢复状态时拒绝调用，避免误清正常账本。 */
mapping rebuild_quest_ledger_from_corrupt()
{
    string file;
    string backup;

    if (!_quest_restore_failed)
        return ([ "ok": 0, "reason": "ledger is not in recovery_pending" ]);
    file = get_quest_save_file();
    backup = file + ".corrupt." + time();
    if (file_size(file + ".o") > 0
        && rename(file + ".o", backup + ".o") != 0)
        return ([ "ok": 0, "reason": "corrupt_backup_failed",
            "source": file + ".o", "backup": backup + ".o" ]);
    _player_quests = ([]);
    _public_quest_runtime = ([]);
    _quest_restore_failed = 0;
    log_file("system/quest", sprintf(
        "[%s] quest ledger rebuilt from corrupt archive; backup=%s\n",
        ctime(time()), backup + ".o"));
    return ([ "ok": 1, "backup": backup + ".o" ]);
}

private string query_quest_player_key(object player)
{
    return call_other(QUEST_STATE_STORE_HELPER, "query_player_state_key", player);
}

private string query_public_quest_cache_key(object player)
{
    return query_quest_player_key(player);
}

private mapping query_public_quest_runtime_entry(object player)
{
    mapping entry;
    mapping data;
    string cache_key;

    cache_key = query_public_quest_cache_key(player);
    if (cache_key == "")
        return 0;
    entry = _public_quest_runtime[cache_key];
    if (mapp(entry))
        return entry;

    data = call_other(QUEST_STATE_STORE_HELPER, "ensure_runtime_data",
        copy_quest_runtime_data(_player_quests[cache_key]), _quest_definitions);
    if (!mapp(data))
        return 0;
    entry = ([
        "player_id": cache_key,
        "section_revision": data["ledger_revision"] || 0,
        "data": data,
    ]);
    _public_quest_runtime[cache_key] = entry;
    return entry;
}

private int persist_public_quest_runtime(object player, mapping data,
    int expected_revision)
{
    mapping entry;
    mapping previous_data;
    string player_key;
    int had_previous;

    entry = query_public_quest_runtime_entry(player);
    if (!mapp(entry) || !mapp(data)
        || entry["section_revision"] != expected_revision)
        return 0;
    player_key = query_quest_player_key(player);
    if (player_key == "")
        return 0;
    had_previous = !undefinedp(_player_quests[player_key]);
    previous_data = had_previous
        ? copy_quest_runtime_data(_player_quests[player_key]) : ([]);
    data["ledger_revision"] = expected_revision + 1;
    _player_quests[player_key] = copy_quest_runtime_data(data);
    {
        int save_result;

        save_result = save_quest_data();
        if (save_result == 0) {
            if (!mapp(_last_quest_save_phase)
                || _last_quest_save_phase["outcome"] == "not_written") {
                if (had_previous)
                    _player_quests[player_key] = previous_data;
                else
                    map_delete(_player_quests, player_key);
                entry["data"] = had_previous ? previous_data : ([]);
                return 0;
            }
            /* Unknown publication must retain the candidate and its original
             * owner receipt. It is not a not-written rollback. */
            log_file("system/quest", sprintf(
                "[%s] public quest save outcome unknown: %s phase=%O\\n",
                ctime(time()), player_key, _last_quest_save_phase));
            return -1;
        }
        entry["section_revision"] = data["ledger_revision"];
        entry["data"] = data;
        return save_result;
    }
}

void invalidate_public_quest_runtime(object player)
{
    string cache_key;

    if (!objectp(player))
        return;
    cache_key = query_public_quest_cache_key(player);
    if (cache_key != "")
        map_delete(_public_quest_runtime, cache_key);
    player->delete_temp("quest/map_marker_projection_cache");
    /* 进行中的 mutation 批次自带回滚能力，此处不得销毁：finish_quest_mutation
     * 解栈时自会清理。无条件 delete 会让中途失效调用静默丢失回滚能力。 */
    if (!mapp(player->query_temp("quest/mutation_batch")))
        player->delete_temp("quest/mutation_batch");
}

mapping flush_public_quest_state(object player, string checkpoint_id)
{
    mapping entry;
    mapping state;

    if (!objectp(player))
        return ([ "ok": 0, "error": "public quest flush is invalid" ]);
    state = player->query_temp("quest/mutation_batch");
    if (mapp(state))
        return ([ "ok": 0, "error": "public quest mutation is active",
            "mutation_depth": state["depth"] || 0,
            "mutation_age_seconds": time()
                - (intp(state["last_touched_at"]) ? state["last_touched_at"]
                    : (intp(state["started_at"]) ? state["started_at"] : time())),
            "mutation_stale": is_stale_quest_mutation(state) ? 1 : 0 ]);
    entry = query_public_quest_runtime_entry(player);
    if (!mapp(entry))
        return ([ "ok": 0, "error": "public quest runtime is unavailable" ]);
    return ([
        "ok": 1,
        "player_id": query_quest_player_key(player),
        "section_id": "public_quest_ledger",
        "revision": entry["section_revision"] || 0,
        "checkpoint_id": checkpoint_id || "",
    ]);
}

int save_quest_data()
{
    mapping detail;
    string file;
    string runtime_dir;

    /* L-140：待恢复状态下拒绝保存，避免空/半成品账本覆盖唯一存档。 */
    if (_quest_restore_failed) {
        _last_quest_save_phase = ([ "outcome": "not_written", "persisted": 0,
            "save_result": 0, "error": "quest restore failed" ]);
        return 0;
    }
    file = get_quest_save_file();
    runtime_dir = DATA_DIR + "runtime";
    if (file_size(runtime_dir) == -1)
        mkdir(runtime_dir);
    detail = owner_save_object_phases(file, "owner/global/service/quest",
        "quest_state", ([
            "player_count": mapp(_player_quests) ? sizeof(_player_quests) : 0,
            "state_digest": hash("sha256", save_variable(_player_quests || ([]))),
        ]), "quest/save/" + hash("sha256", save_variable(_player_quests || ([]))));
    _last_quest_save_phase = mapp(detail) ? copy(detail)
        : ([ "outcome": "unknown", "persisted": -1, "save_result": 0,
            "error": "quest owner save result unavailable" ]);
    if (!mapp(detail) || detail["persisted"] != 1)
        return 0;
    return detail["ok"] == 1 ? 1 : 2;
}

mapping query_quest_save_phase()
{
    return mapp(_last_quest_save_phase) ? copy(_last_quest_save_phase)
        : ([ "outcome": "not_attempted", "persisted": 0, "save_result": 0 ]);
}

int purge_player_quest_data(string player_id)
{
    mapping previous_data;
    int save_code;

    if (!stringp(player_id) || player_id == "")
        return 0;
    if (!mapp(_player_quests) || undefinedp(_player_quests[player_id]))
        return 0;
    previous_data = copy_quest_runtime_data(_player_quests[player_id]);
    map_delete(_player_quests, player_id);
    save_code = save_quest_data();
    if (save_code == 0) {
        if (!mapp(_last_quest_save_phase)
            || _last_quest_save_phase["outcome"] == "not_written")
            _player_quests[player_id] = previous_data;
        return 0;
    }
    map_delete(_public_quest_runtime, player_id);
    return save_code;
}

mapping query_health_snapshot()
{
    return ([
        "status": _quest_restore_failed ? "recovery_pending" : "ok",
        "restore_failed": _quest_restore_failed ? 1 : 0,
        "runtime_section": "public_quest_ledger",
        "active_player_runtime_count": mapp(_public_quest_runtime)
            ? sizeof(_public_quest_runtime) : 0,
        "player_count": mapp(_player_quests) ? sizeof(_player_quests) : 0,
        "state_scope": "character",
        "identity_fields": ({ "quest_id", "acceptance_id",
            "acceptance_revision", "objective_id" }),
    ]);
}

/* §10.3：escort 滞留目标登录恢复 facade——委托 escort runtime 子模块
 * （与登出侧 release_player_escort_targets 同模式）。 */
mapping recover_player_escort_leases_facade(object player)
{
    mapping result;
    mapping mutation_release;
    mapping reward_repair;

    /* L-144：登录恢复同时释放异常残留（超时）的 mutation 批次，避免该玩家
     * 的任务状态永久停止持久化；活跃批次保持不变。
     * L-141：登录同时重试“奖励已发、状态回滚”的补偿队列（幂等，不重复发奖）。 */
    mutation_release = release_stale_quest_mutation(player);
    reward_repair = repair_quest_reward_pending(player);
    result = call_other("/daemons/services/gameplay/quest/quest_escort_runtime",
        "recover_player_escort_leases", player);
    if (mapp(result) && mapp(mutation_release)
        && mutation_release["state"] == "released")
        result += ([ "quest_mutation_release": mutation_release ]);
    if (mapp(result) && mapp(reward_repair) && reward_repair["repaired"] > 0)
        result += ([ "quest_reward_repair": reward_repair ]);
    return result;
}

void create()
{
    ::create();

    /* escort 滞留目标登录恢复经 player_lifecycle manifest 装配
     * （handler=QUEST_D recover_player_escort_leases_facade），本 daemon
     * 不再 create 时注册。 */
    quest_definition_generation = time();
    if (quest_definition_generation <= 0)
        quest_definition_generation = 1;

    if (!mapp(_quest_definitions))
        _quest_definitions = ([]);
    restore_quest_data();
    if (!mapp(_player_quests))
        _player_quests = ([]);
    _public_quest_runtime = ([]);

    load_default_quest_definitions();
    reload_runtime_blueprints();
    load_object(WORLD_ORCHESTRATOR_D);
}

mapping query_player_quest_vars(object player)
{
    mapping data;

    data = ensure_player_data(player);
    if (!mapp(data["player_vars"]))
        data["player_vars"] = ([]);
    return copy(data["player_vars"]);
}

mixed query_player_quest_var(object player, string key)
{
    mapping vars;

    vars = query_player_quest_vars(player);
    return vars[key];
}

int query_quest_definition_generation()
{
    return quest_definition_generation > 0 ? quest_definition_generation : 1;
}

int query_player_map_projection_generation(object player)
{
    mixed generation;

    if (!objectp(player))
        return 0;
    generation = player->query_temp("quest/map_projection_generation");
    return intp(generation) && generation >= 0 ? generation : 0;
}

void mark_player_map_projection_changed(object player)
{
    int generation;

    if (!objectp(player))
        return;
    generation = query_player_map_projection_generation(player) + 1;
    if (generation <= 0)
        generation = 1;
    player->set_temp("quest/map_projection_generation", generation);
    player->delete_temp("quest/map_marker_projection_cache");
    player->delete_temp("xk/exit_projection_cache");
    if (function_exists("schedule_owner_map_grid_read_model_refresh", player))
        call_other(player, "schedule_owner_map_grid_read_model_refresh");
}

int set_player_quest_var(object player, string key, mixed value)
{
    mapping data;
    mapping mutation;
    int owns_mutation;

    if (!objectp(player)
        || !stringp(key) || key == "")
        return 0;

    mutation = player->query_temp("quest/mutation_batch");
    owns_mutation = !mapp(mutation);
    if (owns_mutation && !begin_quest_mutation(player))
        return 0;
    data = ensure_player_data(player);
    if (!mapp(data)) {
        if (owns_mutation)
            finish_quest_mutation(player, -1, 0);
        return 0;
    }
    if (!mapp(data["player_vars"]))
        data["player_vars"] = ([]);
    if (!undefinedp(data["player_vars"][key])
        && data["player_vars"][key] == value) {
        if (owns_mutation)
            finish_quest_mutation(player, 0, 0);
        return 0;
    }
    data["player_vars"][key] = value;
    mutation = player->query_temp("quest/mutation_batch");
    if (!mapp(mutation))
        return 0;
    mutation["dirty"] = 1;
    player->set_temp("quest/mutation_batch", mutation);
    if (owns_mutation)
        return finish_quest_mutation(player, 1, 0);
    return 1;
}

private void load_default_quest_definitions()
{
    mapping defaults;
    string quest_id;
    mapping quest_def;

    _quest_definitions = ([]);

    defaults = call_other(QUEST_CATALOG_HELPER, "build_default_quest_definitions");
    if (!mapp(defaults))
        return;

    foreach (quest_id, quest_def in defaults) {
        if (!stringp(quest_id) || quest_id == "" || !mapp(quest_def))
            continue;
        _quest_definitions[quest_id] = copy(quest_def);
    }
}

void reload_runtime_blueprints()
{
    mapping runtime_defs;
    string quest_id;
    mapping quest_def;

    load_default_quest_definitions();
    quest_definition_generation++;
    if (quest_definition_generation <= 0)
        quest_definition_generation = 1;
    runtime_defs = QUEST_BLUEPRINT_D->query_published_definitions();
    if (!mapp(runtime_defs))
        return;

    foreach (quest_id, quest_def in runtime_defs) {
        if (!stringp(quest_id) || quest_id == "" || !mapp(quest_def))
            continue;
        if (mapp(_quest_definitions[quest_id])) {
            log_file("game/quest", sprintf("[%s] skip runtime blueprint conflict: %s\n",
                ctime(time())[4..19], quest_id));
            continue;
        }
        _quest_definitions[quest_id] = copy(quest_def);
    }
}

void refresh_live_projection_state(object player)
{
    object room;

    if (!objectp(player))
        return;

    room = environment(player);
    if (!objectp(room))
        return;

    WORLD_ORCHESTRATOR_D->refresh_player_world_state(player, room, 1);
}

mapping ensure_player_data(object player)
{
    mapping data;
    mapping entry;

    entry = query_public_quest_runtime_entry(player);
    if (!mapp(entry))
        return 0;
    data = call_other(QUEST_STATE_STORE_HELPER, "ensure_runtime_data",
        entry["data"], _quest_definitions);
    entry["data"] = data;
    return data;
}

int player_has_quest_event_state(object player)
{
    mapping data;
    string bucket;

    data = ensure_player_data(player);
    if (!mapp(data))
        return 0;
    foreach (bucket in ({ "active", "completed", "abandoned" })) {
        if (mapp(data[bucket]) && sizeof(data[bucket]) > 0)
            return 1;
    }
    return 0;
}

private mapping query_player_quest_record(object player, string quest_id)
{
    return call_other(QUEST_QUERY_HELPER, "query_player_quest_record",
        ensure_player_data(player), quest_id);
}

mapping capture_quest_sync_snapshot(mapping data)
{
    mapping snapshot;
    mapping quest_info;
    string bucket;
    string quest_id;

    snapshot = ([]);
    if (!mapp(data))
        return snapshot;

    foreach (bucket in ({ "active", "completed", "abandoned" })) {
        if (!mapp(data[bucket]))
            continue;
        foreach (quest_id, quest_info in data[bucket]) {
            if (!stringp(quest_id) || quest_id == "" || !mapp(quest_info))
                continue;
            snapshot[quest_id] = ([
                "status": quest_info["status"],
                "progress": quest_info["progress"],
                "current_step": quest_info["current_step"] || 0,
                "step_type": arrayp(quest_info["steps"])
                    && (quest_info["current_step"] || 0) >= 0
                    && (quest_info["current_step"] || 0) < sizeof(quest_info["steps"])
                    && mapp(quest_info["steps"][quest_info["current_step"] || 0])
                        ? quest_info["steps"][quest_info["current_step"] || 0]["type"] : "",
                "signature": save_variable(quest_info),
            ]);
        }
    }

    return snapshot;
}

int begin_quest_mutation(object player)
{
    mapping data;
    mapping entry;
    mapping state;

    if (!objectp(player))
        return 0;
    state = player->query_temp("quest/mutation_batch");
    if (mapp(state) && is_stale_quest_mutation(state)) {
        /* L-144：异常跳过 finish 的批次不得永久占据 depth>0（否则该玩家
         * 之后的 mutation 全部提前返回、状态不再持久化）。过期批次按失败
         * 回滚并释放（日志在释放函数内），随后按常规开启新批次。 */
        release_stale_quest_mutation(player);
        state = 0;
    }
    if (!mapp(state)) {
        data = ensure_player_data(player);
        entry = query_public_quest_runtime_entry(player);
        if (!mapp(data) || !mapp(entry))
            return 0;
        state = ([
            "depth": 0,
            "dirty": 0,
            "failed": 0,
            "refresh_world": 0,
            "pending_events": ({ }),
            "before": capture_quest_sync_snapshot(data),
            "before_data": restore_variable(save_variable(data)),
            "section_revision": entry["section_revision"] || 0,
            "started_at": time(),
            "last_touched_at": time(),
            "response_context": CLIENT_SYNC_D->capture_command_response_context(player),
        ]);
        player->set_temp("quest/pending_reward_feedback", ({ }));
    }
    state["depth"] = (state["depth"] || 0) + 1;
    state["last_touched_at"] = time();
    player->set_temp("quest/mutation_batch", state);
    return 1;
}

/* L-144：批次是否超过残留阈值未推进。开始时间兜底取 last_touched_at。 */
int is_stale_quest_mutation(mapping state)
{
    int last_touched;

    if (!mapp(state))
        return 0;
    last_touched = intp(state["last_touched_at"]) ? state["last_touched_at"]
        : (intp(state["started_at"]) ? state["started_at"] : 0);
    if (last_touched <= 0)
        return 1;
    return time() - last_touched > QUEST_MUTATION_STALE_SECONDS;
}

/* L-144：释放异常残留的 mutation 批次（仅超时批次）。回滚到批次快照并
 * 丢弃反馈队列；未超时的活跃批次保持不变。返回结构化结果供登录/诊断消费。 */
mapping release_stale_quest_mutation(object player)
{
    mapping state;
    int depth;
    int age;

    if (!objectp(player))
        return ([ "ok": 0, "reason": "invalid_player" ]);
    state = player->query_temp("quest/mutation_batch");
    if (!mapp(state))
        return ([ "ok": 1, "state": "absent" ]);
    if (!is_stale_quest_mutation(state))
        return ([ "ok": 0, "state": "active",
            "depth": state["depth"] || 0 ]);
    depth = state["depth"] || 0;
    age = time() - (intp(state["last_touched_at"]) ? state["last_touched_at"]
        : (intp(state["started_at"]) ? state["started_at"] : time()));
    player->delete_temp("quest/mutation_batch");
    restore_quest_mutation_snapshot(player, state);
    take_pending_reward_feedback(player);
    log_file("system/quest", sprintf(
        "[%s] stale quest mutation released: player=%s depth=%d age=%ds\n",
        ctime(time()), PLAYER_STATE_D->query_player_id(player), depth, age));
    return ([ "ok": 1, "state": "released", "depth": depth, "age_seconds": age ]);
}

/*
 * Emit a quest lifecycle event to the shared event dispatcher. This is the
 * only emission point; lifecycle code queues events and the outermost mutation
 * flush calls this after a durable commit.
 */
private void emit_quest_event(object player, string event_name, string quest_id)
{
    EVENT_D->emit(event_name, ([
        "player_id": objectp(player) ? PLAYER_STATE_D->query_player_id(player) : "",
        "quest_id": quest_id,
        "timestamp": time()
    ]));
}

/*
 * Queue a player quest event that must only be emitted after the outermost
 * mutation has durably committed. Rollback discards the queue together with
 * the batch state, so a failed or rolled-back mutation never reports an event
 * that did not happen. Callers without an active batch emit immediately to
 * preserve the pre-mutation behaviour of non-transactional paths.
 */
void queue_quest_event(object player, string event_name, string quest_id)
{
    mapping state;
    mixed *events;

    if (!objectp(player) || !stringp(event_name) || event_name == "")
        return;
    state = player->query_temp("quest/mutation_batch");
    if (!mapp(state)) {
        emit_quest_event(player, event_name, quest_id);
        return;
    }
    events = state["pending_events"];
    if (!arrayp(events))
        events = ({ });
    events += ({ ([ "event": event_name, "quest_id": quest_id ]) });
    state["pending_events"] = events;
    player->set_temp("quest/mutation_batch", state);
}

private void flush_pending_quest_events(object player, mapping state)
{
    mixed *events;
    mapping entry;
    string event_name;
    string quest_id;

    if (!objectp(player) || !mapp(state))
        return;
    events = state["pending_events"];
    if (!arrayp(events))
        return;
    foreach (entry in events) {
        if (!mapp(entry))
            continue;
        event_name = entry["event"];
        quest_id = entry["quest_id"];
        if (!stringp(event_name) || event_name == "")
            continue;
        emit_quest_event(player, event_name, quest_id);
    }
}

/* 登出/断线释放 facade——实现单一 owner 在 quest_escort_runtime，
 * 此处只转发结构化结果。 */
mapping release_player_escort_targets(object player)
{
    return call_other(QUEST_ESCORT_RUNTIME_HELPER, "release_player_escort_targets",
        player);
}

int finish_quest_mutation(object player, int succeeded, int refresh_world)
{
    mapping data;
    mapping state;
    mixed *reward_feedback;
    string *changed;
    string *pending_turn_in;
    string *reported_ids;
    object room;
    mixed err;
    int persistence_result;

    if (!objectp(player))
        return 0;
    state = player->query_temp("quest/mutation_batch");
    if (!mapp(state))
        return 0;
    /* Mutation results are tri-state: 1 changed data, 0 no-op, -1 failed.
     * A no-op must close only its own nesting level and must not advance the
     * public quest domain revision. */
    if (succeeded > 0)
        state["dirty"] = 1;
    else if (succeeded < 0)
        state["failed"] = 1;
    if (refresh_world)
        state["refresh_world"] = 1;
    state["depth"] = (state["depth"] || 1) - 1;
    state["last_touched_at"] = time();
    if (state["depth"] > 0) {
        player->set_temp("quest/mutation_batch", state);
        return succeeded < 0 ? 0 : 1;
    }
    player->delete_temp("quest/mutation_batch");
    if (state["failed"]) {
        reward_feedback = take_pending_reward_feedback(player);
        restore_quest_mutation_snapshot(player, state);
        flush_pending_reward_feedback(player, reward_feedback, ({ }),
            state["response_context"]);
        return 0;
    }
    if (!state["dirty"]) {
        flush_pending_reward_feedback(player, take_pending_reward_feedback(player), ({ }),
            state["response_context"]);
        return 0;
    }

    data = ensure_player_data(player);
    if (!mapp(data)) {
        flush_pending_reward_feedback(player, take_pending_reward_feedback(player), ({ }),
            state["response_context"]);
        return 0;
    }
    data["domain_revision"] = (data["domain_revision"] || 0) + 1;
    if (data["domain_revision"] <= 0)
        data["domain_revision"] = 1;
    // B2 双保险失效：修订推进即丢弃该玩家的 quest-list 读模型缓存
    // （发送器侧四代元组比对已是主失效机制，此处为提前释放内存/防御纵深）
    err = catch(QUEST_PROTOCOL->invalidate_quest_list_read_model(
        PLAYER_STATE_D->query_player_id(player)));
    changed = collect_changed_quest_ids(state["before"], data);
    /* 末步完成即自动交付——在同一 mutation 内 settle+transition，
     * 不再调 submit_quest 开第二提交周期（提交段内重入，persist/
     * sync 双跑、反馈重复触发）。settle 后重算 changed 纳入交付。 */
    pending_turn_in = collect_pending_turn_in_ids(data);
    auto_complete_pending_quests(player, state["before"], data,
        pending_turn_in);
    // 生命周期监听器可能开启下一批 mutation；先取走本批反馈，避免队列被覆盖。
    reward_feedback = take_pending_reward_feedback(player);
    changed = collect_changed_quest_ids(state["before"], data);
    persistence_result = persist_public_quest_runtime(player, data,
        state["section_revision"] || 0);
    if (persistence_result == 0) {
        restore_quest_mutation_snapshot(player, state);
        invalidate_public_quest_runtime(player);
        /* 奖励已在 settle 阶段发放（settle_reward 带 player+quest+acceptance+revision
         * 幂等键，重做不会重复发奖）：保持「已发即提示」语义照常 flush。
         * L-141：把本批已结算的任务写入持久补偿队列，由
         * repair_quest_reward_pending() 幂等推进到一致（不重复发奖）；
         * 「奖励已发、任务状态回滚」这一非原子态既不可静默也不可永久
         * 不一致。 */
        record_quest_reward_repair_pending(player, pending_turn_in, data);
        flush_pending_reward_feedback(player, reward_feedback, ({ }),
            state["response_context"]);
        log_file("system/quest", sprintf(
            "[%s] quest persist failed, mutation rolled back: %s\n",
            ctime(time()), PLAYER_STATE_D->query_player_id(player)));
        return 0;
    }
    if (persistence_result < 0) {
        /* The publication is unknown. Keep the candidate and the owner
         * receipt responsibility; never run a compensating business rollback
         * or emit a successful mutation result. */
        player->delete_temp("quest/mutation_batch");
        flush_pending_reward_feedback(player, reward_feedback, ({ }),
            state["response_context"]);
        log_file("system/quest", sprintf(
            "[%s] quest publication unknown; candidate retained: %s phase=%O\\n",
            ctime(time()), PLAYER_STATE_D->query_player_id(player),
            _last_quest_save_phase));
        return 0;
    }
    if (persistence_result == 2) {
        /* The quest candidate is durable, but its owner receipt is not closed.
         * Keep the mutation out of success feedback and let the original
         * publication retry deliver the same result later. */
        player->delete_temp("quest/mutation_batch");
        log_file("system/quest", sprintf(
            "[%s] quest state persisted with receipt pending: %s\n",
            ctime(time()), PLAYER_STATE_D->query_player_id(player)));
        return 2;
    }
    /* 统一出口：护送目标释放挂在状态转换唯一收口（持久化成功后），
     * complete/fail/abandon/turn-in/auto-submit 全部终态路径经此释放，
     * 新增终态路径无需逐个补调用点。 */
    call_other(QUEST_ESCORT_RUNTIME_HELPER, "release_escort_targets_for_terminal",
        player, state["before_data"], data);
    /* Lifecycle listeners update world flags synchronously. Flush them before
     * the single authoritative room refresh so NPC markers and projections
     * are already converged when that snapshot is encoded. */
    flush_pending_quest_events(player, state);
    mark_player_map_projection_changed(player);
    emit_quest_sync_for_changes(player, changed, state["response_context"]);
    reported_ids = emit_quest_mutation_feedback(player, state["before"],
        changed, reward_feedback, state["response_context"]);
    flush_pending_reward_feedback(player, reward_feedback, reported_ids,
        state["response_context"]);
    /* 交互类步骤推进后重发房间快照——NPC 弹窗右侧动作区
     * 的独立指引按钮由房间投影实时计算，投影内容未变时
     * refresh_player_world_state 的 changed 门会跳过重发，玩家看到旧投影。
     * 门控在交互类步骤：KILL/COLLECT 类进度同一步内高频 tick，按钮不随
     * tick 变化，重发纯浪费。 */
    if (quest_mutation_affects_provider_actions(player, state["before"], changed)
        && objectp(environment(player)))
        call_other("/kernel/simul_efun", "xk_emit_room_snapshot", player,
            environment(player));
    if (state["refresh_world"]) {
        room = environment(player);
        if (objectp(room))
            WORLD_ORCHESTRATOR_D->refresh_player_world_state(player, room, 1);
    }
    return 1;
}

/* L-141：奖励已发、任务状态回滚后的持久补偿队列（事件式，不覆盖写；
 * 键更新经玩家状态 owner 提交）。 */
private int commit_reward_repair_queue(object player, mapping queue)
{
    mapping response;

    if (!objectp(player) || !mapp(queue))
        return 0;
    response = PLAYER_STATE_D->commit_state_fields(player,
        ([ "quest/reward_repair_pending": queue ]),
        PLAYER_STATE_D->build_state_idempotency_key(player,
            "quest/reward_repair_pending", queue));
    if (!mapp(response) || !response["ok"])
        log_file("system/quest", sprintf(
            "[%s] quest reward repair queue persist failed: player=%s\n",
            ctime(time()), PLAYER_STATE_D->query_player_id(player)));
    return mapp(response) && response["ok"];
}

private void record_quest_reward_repair_pending(object player,
    string *quest_ids, mapping data)
{
    mapping queue;
    mapping quest_info;
    string quest_id;

    if (!objectp(player) || !arrayp(quest_ids) || sizeof(quest_ids) == 0)
        return;
    queue = player->query("quest/reward_repair_pending");
    if (!mapp(queue))
        queue = ([]);
    foreach (quest_id in quest_ids) {
        quest_info = mapp(data) && mapp(data["completed"])
            ? data["completed"][quest_id] : 0;
        if (!mapp(quest_info) && mapp(data) && mapp(data["active"]))
            quest_info = data["active"][quest_id];
        if (!mapp(quest_info))
            continue;
        queue[quest_id] = ([
            "quest_id": quest_id,
            "acceptance_id": quest_info["acceptance_id"] || "",
            "acceptance_revision": quest_info["acceptance_revision"] || 0,
            "recorded_at": time(),
        ]);
    }
    commit_reward_repair_queue(player, queue);
}

/* L-141：幂等修复入口——把补偿队列里的任务推进到 DELIVERED。
 * settle_turn_in 复用同一 acceptance 幂等键，重试不会重复发奖；
 * persist 成功才移除记录，失败保留供下次登录/触发重试。 */
mapping repair_quest_reward_pending(object player)
{
    mapping queue;
    mapping data;
    mapping entry;
    mapping quest_info;
    mapping quest_def;
    string quest_id;
    string key;
    int repaired;
    int failed;

    if (!objectp(player))
        return ([ "ok": 0, "repaired": 0, "failed": 0, "remaining": 0,
            "error": "invalid_player" ]);
    queue = player->query("quest/reward_repair_pending");
    if (!mapp(queue) || sizeof(queue) == 0)
        return ([ "ok": 1, "repaired": 0, "failed": 0, "remaining": 0,
            "state": "empty" ]);
    foreach (key, entry in copy(queue)) {
        if (!mapp(entry))
            continue;
        quest_id = stringp(entry["quest_id"]) ? entry["quest_id"] : key;
        if (!stringp(quest_id) || quest_id == "")
            continue;
        if (!begin_quest_mutation(player)) {
            failed++;
            continue;
        }
        data = ensure_player_data(player);
        quest_def = query_visible_quest_definition(player, quest_id);
        quest_info = mapp(data) && mapp(data["completed"])
            ? data["completed"][quest_id] : 0;
        if (!mapp(quest_info) && mapp(data) && mapp(data["active"]))
            quest_info = data["active"][quest_id];
        if (!mapp(data) || !mapp(quest_def) || !mapp(quest_info)) {
            /* 任务或定义已不存在：无状态可修，移除记录。 */
            finish_quest_mutation(player, 0, 0);
            map_delete(queue, key);
            repaired++;
            continue;
        }
        if ((quest_info["status"] || 0) == QUEST_STATUS_DELIVERED) {
            /* 已一致（例如重试前被正常流程补齐）：移除记录。 */
            finish_quest_mutation(player, 0, 0);
            map_delete(queue, key);
            repaired++;
            continue;
        }
        if ((quest_info["status"] || 0) != QUEST_STATUS_COMPLETED) {
            /* 状态不是可交付（如已被放弃/失败）：无法推进，保留记录待人工。 */
            finish_quest_mutation(player, 0, 0);
            failed++;
            continue;
        }
        if (!call_other(QUEST_SETTLEMENT_D, "settle_turn_in", player,
            quest_id, quest_def, quest_info)
            || !call_other(QUEST_TRANSITION_D, "apply_transition", data,
                quest_id, QUEST_STATUS_DELIVERED)) {
            finish_quest_mutation(player, -1, 0);
            failed++;
            continue;
        }
        if (finish_quest_mutation(player, 1, 0)) {
            map_delete(queue, key);
            repaired++;
        } else {
            /* persist 再次失败：状态已回滚，记录保留供下一次重试。 */
            failed++;
        }
    }
    commit_reward_repair_queue(player, queue);
    return ([ "ok": failed == 0, "repaired": repaired, "failed": failed,
        "remaining": sizeof(queue) ]);
}

private int quest_mutation_affects_provider_actions(object player,
    mapping before_snapshot, string *quest_ids)
{
    mapping data;
    mapping quest_info;
    mapping before_entry;
    mixed *steps;
    mixed step;
    string quest_id;
    int index;

    if (!objectp(player) || !mapp(before_snapshot) || !arrayp(quest_ids)
        || sizeof(quest_ids) == 0)
        return 0;
    data = ensure_player_data(player);
    if (!mapp(data) || !mapp(data["active"]))
        return 0;
    foreach (quest_id in quest_ids) {
        /* 前后任一侧是交互类步骤都要重发：按钮出现/变化/消失都依赖投影刷新。
         * 类型判定走 is_interactive_step_type 单一谓词（与权威一致）。 */
        before_entry = before_snapshot[quest_id];
        if (mapp(before_entry)
            && is_interactive_step_type(before_entry["step_type"]))
            return 1;
        quest_info = data["active"][quest_id];
        if (!mapp(quest_info))
            continue;
        steps = quest_info["steps"];
        index = quest_info["current_step"] || 0;
        if (!arrayp(steps) || index < 0 || index >= sizeof(steps))
            continue;
        step = steps[index];
        if (!mapp(step))
            continue;
        if (is_interactive_step_type(step["type"]))
            return 1;
    }
    return 0;
}

/* 同一任务的进度、终态与实发奖励只形成一个反馈块。 */
private string *emit_quest_mutation_feedback(object player, mapping before_snapshot,
    string *quest_ids, mixed *feedback, mapping response_context)
{
    mapping data;
    mapping quest_info;
    mapping before_entry;
    mapping rewards_by_quest;
    mixed entry;
    string quest_id;
    string text;
    string *feedback_lines;
    string *reported_ids;
    int before_step;
    int after_step;
    int before_status;
    int after_status;
    int delivered;

    reported_ids = ({ });
    if (!objectp(player) || !mapp(before_snapshot) || !arrayp(quest_ids))
        return reported_ids;
    data = ensure_player_data(player);
    if (!mapp(data))
        return reported_ids;
    rewards_by_quest = ([ ]);
    foreach (entry in arrayp(feedback) ? feedback : ({ })) {
        if (!mapp(entry) || !stringp(entry["quest_id"]) || !arrayp(entry["lines"]))
            continue;
        quest_id = entry["quest_id"];
        if (!arrayp(rewards_by_quest[quest_id]))
            rewards_by_quest[quest_id] = ({ });
        rewards_by_quest[quest_id] += entry["lines"];
    }
    foreach (quest_id in quest_ids) {
        before_entry = before_snapshot[quest_id];
        quest_info = data["active"][quest_id];
        if (!mapp(quest_info))
            quest_info = data["completed"][quest_id];
        if (!mapp(quest_info))
            continue;
        before_status = mapp(before_entry) ? (before_entry["status"] || 0) : QUEST_STATUS_AVAILABLE;
        after_status = quest_info["status"] || 0;
        delivered = after_status == QUEST_STATUS_DELIVERED;
        if (before_status == QUEST_STATUS_DELIVERED)
            continue;
        if (after_status != QUEST_STATUS_ACCEPTED
            && after_status != QUEST_STATUS_COMPLETED && !delivered)
            continue;
        if (before_status == QUEST_STATUS_AVAILABLE && !delivered)
            continue;
        before_step = mapp(before_entry) ? (before_entry["current_step"] || 0) : 0;
        after_step = quest_info["current_step"] || 0;
        if (after_step == before_step && after_status == before_status)
            continue;
        feedback_lines = ({ });
        if (delivered)
            feedback_lines += ({ "已完成！" });
        else if (after_status == QUEST_STATUS_COMPLETED)
            feedback_lines += ({ "目标已达成，奖励结算尚未完成。" });
        if (arrayp(rewards_by_quest[quest_id]))
            feedback_lines += rewards_by_quest[quest_id];
        text = render_quest_step_text(player, quest_id, feedback_lines);
        if (text == "")
            continue;
        MESSAGE_D->emit_message_event(player, text, ([
            "channel": "main",
            "intent": "notify",
            "priority": "normal",
            "reliability": "important",
            "display_mode": "instant",
            "collapse_key": "quest/" + (delivered ? "complete/" : "step_progress/") + quest_id,
            "response_context": response_context,
            "payload": ([
                "kind": delivered ? "quest_complete" : "quest_progress",
                "quest_id": quest_id,
            ]),
        ]));
        reported_ids += ({ quest_id });
    }
    return reported_ids;
}

/* 末步完成自动交付——在本 mutation 提交窗口内（persist 前）
 * 直接结算并转 DELIVERED，不再调公共 submit_quest（其 begin/finish 会
 * 开第二提交周期，persist/sync 双跑）。语义与 submit_quest 一致：绕过
 * 手动交付门（can_turn_in_quest 的在场校验），只要求状态 COMPLETED；
 * 单任务失败只跳过该任务，不回滚整个 mutation。
 * 返回后由 mutation 重算 changed，并按任务归并实际奖励反馈。 */
private void auto_complete_pending_quests(object player, mapping before_snapshot,
    mapping data, string *quest_ids)
{
    mapping quest_def;
    mapping quest_info;
    string quest_id;
    int after_status;

    if (!objectp(player) || !mapp(data) || !arrayp(quest_ids))
        return;
    foreach (quest_id in quest_ids) {
        quest_info = data["active"][quest_id];
        if (!mapp(quest_info))
            quest_info = data["completed"][quest_id];
        if (!mapp(quest_info))
            continue;
        after_status = quest_info["status"] || 0;
        if (after_status != QUEST_STATUS_COMPLETED)
            continue;
        quest_def = query_visible_quest_definition(player, quest_id);
        if (!mapp(quest_def))
            continue;
        if (!call_other(QUEST_SETTLEMENT_D, "settle_turn_in", player,
            quest_id, quest_def, quest_info))
            continue;
        if (!call_other(QUEST_TRANSITION_D,
            "apply_transition", data, quest_id, QUEST_STATUS_DELIVERED))
            continue;
    }
}

/* 待自动交付的任务集合：completed 桶内仍处于 COMPLETED 的任务（含历史遗留与
 * 本次刚完成）。直接扫状态，不依赖 changed 的签名抖动，避免遗留任务永远
 * 停在「回去交付」。 */
private string *collect_pending_turn_in_ids(mapping data)
{
    mapping quest_info;
    string *ids;
    string quest_id;

    ids = ({ });
    if (!mapp(data) || !mapp(data["completed"]))
        return ids;
    foreach (quest_id, quest_info in data["completed"]) {
        if (!mapp(quest_info))
            continue;
        if ((quest_info["status"] || 0) == QUEST_STATUS_COMPLETED)
            ids += ({ quest_id });
    }
    return ids;
}

private mixed *take_pending_reward_feedback(object player)
{
    mixed *feedback;

    feedback = player->query_temp("quest/pending_reward_feedback");
    player->delete_temp("quest/pending_reward_feedback");
    return arrayp(feedback) ? feedback : ({ });
}

/* 正常反馈已并入任务块；失败只报告到账事实，绝不冒充任务完成。
 * 不论文案是否合并，状态、背包和落地物品同步都保留。 */
private void flush_pending_reward_feedback(object player, mixed *feedback,
    string *reported_ids, mapping response_context)
{
    mixed entry;
    string *lines;

    if (!objectp(player) || !arrayp(feedback))
        return;
    foreach (entry in feedback) {
        if (!mapp(entry) || !mapp(entry["applied"]))
            continue;
        lines = arrayp(entry["lines"]) ? entry["lines"] : ({ });
        if (sizeof(lines) && member_array(entry["quest_id"], reported_ids) == -1) {
            lines = ({ "奖励已到账，任务状态尚未确认。" }) + lines;
            MESSAGE_D->emit_message_event(player,
                render_quest_reward_text(entry["quest_name"], lines), ([
                "channel": "main",
                "intent": "notify",
                "reliability": "important",
                "display_mode": "instant",
                "response_context": response_context,
                "payload": ([ "kind": "reward_gain", "quest_id": entry["quest_id"] ]),
            ]));
        }
        REWARD_FEEDBACK_D->sync_reward_state(player, entry["applied"],
            response_context);
    }
}

private void restore_quest_mutation_snapshot(object player, mapping state)
{
    mapping before_data;
    mapping entry;

    if (!objectp(player) || !mapp(state))
        return;
    before_data = state["before_data"];
    if (!mapp(before_data))
        return;
    entry = query_public_quest_runtime_entry(player);
    if (!mapp(entry))
        return;
    entry["data"] = restore_variable(save_variable(before_data));
}

string *collect_changed_quest_ids(mapping before_snapshot, mapping data)
{
    mapping after_snapshot;
    mapping before_entry;
    mapping after_entry;
    mapping seen;
    string *changed;
    string quest_id;

    after_snapshot = capture_quest_sync_snapshot(data);
    seen = ([]);
    changed = ({ });

    foreach (quest_id in keys(before_snapshot || ([]))) {
        if (seen[quest_id])
            continue;
        seen[quest_id] = 1;
        before_entry = before_snapshot[quest_id];
        after_entry = after_snapshot[quest_id];
        if (!mapp(before_entry) || !mapp(after_entry)
            || before_entry["signature"] != after_entry["signature"])
            changed += ({ quest_id });
    }

    foreach (quest_id in keys(after_snapshot)) {
        if (seen[quest_id])
            continue;
        changed += ({ quest_id });
    }

    return changed;
}

void emit_quest_sync_for_changes(object player, string *quest_ids,
    mapping response_context)
{
    mapping data;
    mapping quest_info;
    mapping seen;
    string quest_id;
    string delta_id;
    string operation;
    int delta_index;
    int domain_revision;

    if (!objectp(player))
        return;
    data = ensure_player_data(player);
    domain_revision = mapp(data) ? (data["domain_revision"] || 0) : 0;
    if (!interactive(player))
        return;

    seen = ([]);
    if (arrayp(quest_ids)) {
        foreach (quest_id in sort_array(quest_ids, 1)) {
            if (!stringp(quest_id) || quest_id == "" || seen[quest_id])
                continue;
            seen[quest_id] = 1;
            quest_info = call_other(QUEST_QUERY_HELPER,
                "query_player_quest_record", data, quest_id);
            operation = mapp(quest_info)
                && member_array(quest_info["status"], ({
                    QUEST_STATUS_ACCEPTED, QUEST_STATUS_COMPLETED })) != -1
                && is_quest_visible_for_player(player, quest_id)
                    ? "upsert" : "remove";
            delta_index++;
            delta_id = sprintf("%d:%d:%s", domain_revision,
                delta_index, quest_id);
            QUEST_PROTOCOL->xk_send_quest_update(player,
                quest_id, operation, domain_revision, delta_id,
                response_context);
        }
    }

    QUEST_PROTOCOL->xk_send_quest_list(player, response_context);
}

private int can_turn_in_quest(object player, string quest_id)
{
    return call_other(QUEST_PROGRESS_D, "can_turn_in",
        query_visible_quest_definitions(player), ensure_player_data(player),
        quest_id);
}

mapping get_all_quests_info()
{
    return call_other(QUEST_CATALOG_HELPER, "get_all_quests_info", _quest_definitions);
}

mapping get_quests_by_type(string type)
{
    return call_other(QUEST_CATALOG_HELPER, "get_quests_by_type", _quest_definitions, type);
}

mapping get_quests_by_provider(string provider_id)
{
    return call_other(QUEST_CATALOG_HELPER, "get_quests_by_provider", _quest_definitions, provider_id);
}

mapping get_quest_definition(string quest_id)
{
    return call_other(QUEST_CATALOG_HELPER, "get_quest_definition", _quest_definitions, quest_id);
}

mapping query_player_quest_runtime(object player, string quest_id)
{
    mapping quest_info;

    if (!mapp(query_visible_quest_definition(player, quest_id))
        || !is_quest_visible_for_player(player, quest_id))
        return 0;
    quest_info = query_player_quest_record(player, quest_id);
    if (!mapp(quest_info))
        return 0;
    return copy(quest_info);
}

mapping query_quest_validation_report()
{
    return call_other(QUEST_VALIDATE_HELPER, "validate_all_quest_definitions", _quest_definitions);
}

string render_quest_validation_report()
{
    return call_other(QUEST_VALIDATE_HELPER, "render_validation_report",
        query_quest_validation_report());
}

int is_quest_visible_for_player(object player, string quest_id)
{
    mapping definitions;
    mapping definition;

    definitions = query_visible_quest_definitions(player);
    definition = definitions[quest_id];
    if (!mapp(definition))
        return 0;
    return call_other(QUEST_VALIDATE_HELPER, "is_quest_visible_for_player",
        definitions, ensure_player_data(player), player, quest_id);
}

int can_accept_quest(object player, string quest_id)
{
    if (!is_quest_visible_for_player(player, quest_id))
        return 0;
    return call_other(QUEST_VALIDATE_HELPER, "can_accept_quest",
        query_visible_quest_definitions(player), ensure_player_data(player),
        player, quest_id);
}

/* provider 点击分流单门面。返回空串 = 应打开任务面板；
 * 非空 = 应展示的提示文本（进行中引导 / 无任务提示），base 层哑转发。
 * 空 provider（非任务 NPC）亦归入提示文本，不再走面板语义。
 * 单次遍历出结果，避免多谓词各自全量扫描的漂移与浪费。 */
string query_provider_click_hint(object player, string provider_id)
{
    mapping data;
    mapping definitions;
    mapping quest_info;
    string quest_id;
    int status;
    int locked_visible;

    /* 空 provider（非任务 NPC）不再返空串——空串=开面板，
     * present_provider_panel 对空 provider 返 0 后会穿透 quest.c default
     * 分支把 npc_id 当任务 id 报「没有找到该任务」。改返无任务提示文本，
     * 玩家得飘窗/主区提示而非报错文案。 */
    if (!objectp(player))
        return "";
    if (!stringp(provider_id) || provider_id == "")
        return query_no_quest_hint();
    data = ensure_player_data(player);
    definitions = query_visible_quests_by_provider(player, provider_id);
    locked_visible = 0;
    if (mapp(definitions)) {
        foreach (quest_id in keys(definitions)) {
            status = get_quest_status(player, quest_id);
            switch (status) {
            case QUEST_STATUS_AVAILABLE:
                if (can_accept_quest(player, quest_id))
                    return "";
                locked_visible = 1;   /* 前置锁定：飘窗提示尚未解锁 */
                break;
            case QUEST_STATUS_COMPLETED:
                return "";            /* 待交付：面板承载交付钮 */
            case QUEST_STATUS_FAILED:
                quest_info = call_other(QUEST_QUERY_HELPER, "query_player_quest_record",
                    data, quest_id);
                if (mapp(quest_info) && quest_info["recoverable"])
                    return "";
                break;
            case QUEST_STATUS_ACCEPTED:
                /* 当前步正指向该 provider 时开面板——
                 * 面板经指引动作并入携带步骤按钮（如道谢），是 TALK 类收尾步的
                 * 唯一可达入口；其余进行中 NPC 与无任务同文案飘窗，不再用
                 * 「任务还在进行中」专用提示。判定复用
                 * is_map_marker_provider_step_actionable 单一真源。 */
                quest_info = call_other(QUEST_QUERY_HELPER, "query_player_quest_record",
                    data, quest_id);
                if (is_map_marker_provider_step_actionable(quest_info, provider_id))
                    return "";
                break;
            default:
                break;
            }
        }
    }
    if (locked_visible)
        return query_quest_locked_hint();
    return query_no_quest_hint();
}

/* ===== 任务指引动作单源 =====
 * 上提自 cmds/player/quest.c 的 build_provider_progress_actions（原仅服务进度弹窗，
 * 交互面板管线拿不到步骤级动作导致 TALK 类任务卡关）。两个 surface（进度弹窗 +
 * NPC 交互面板）共同消费本权威，禁止在调用侧复制 switch。
 * 标签：优先 step["action_label"]，缺失回退泛化标签。 */

private mapping build_step_guidance_action(mapping step, string fallback_provider)
{
    mapping action;
    string target;
    string item_id;
    string topic;
    string step_action;
    string room;
    string cmd;
    string label;
    string kind;

    if (!mapp(step) || step["completed"])
        return 0;

    target = stringp(step["target"]) && step["target"] != "" ? step["target"] : "";
    item_id = stringp(step["item_id"]) && step["item_id"] != "" ? step["item_id"] : "";
    label = stringp(step["action_label"]) && step["action_label"] != "" ? step["action_label"] : "";
    cmd = "";
    kind = "quest";

    switch (step["type"]) {
    case QUEST_STEP_TALK:
        if (target == "")
            target = fallback_provider;
        if (target == "")
            return 0;
        if (label == "")
            label = "交谈";
        cmd = "talk " + target;
        kind = "talk";
        break;

    case QUEST_STEP_ASK:
        if (target == "")
            target = fallback_provider;
        topic = stringp(step["topic"]) ? step["topic"] : "";
        if (target == "" || topic == "")
            return 0;
        if (label == "")
            label = "询问" + topic;
        cmd = "ask " + target + " about " + topic;
        kind = "ask";
        break;

    case QUEST_STEP_LOOK:
        /* 配置显式 command 优先（如 listen_rumor 的 listen 动词）。 */
        cmd = stringp(step["command"]) ? step["command"] : "";
        room = stringp(step["room"]) ? step["room"] : "";
        if (cmd == "" && target != "")
            cmd = "look " + target;
        if (cmd == "" && target == "" && room != "")
            cmd = "look";
        if (cmd == "")
            return 0;
        if (label == "" && target != "")
            label = "查看";
        if (label == "")
            label = "查看周围";
        kind = "look";
        break;

    case QUEST_STEP_COLLECT:
        cmd = stringp(step["command"]) ? step["command"] : "";
        if (cmd == "" && item_id != "")
            cmd = "get " + item_id;
        if (cmd == "")
            return 0;
        if (label == "")
            label = "拾取";
        kind = "get";
        break;

    case QUEST_STEP_GIVE:
        // 交物走 NPC 固定给予入口，由玩家选择物品和数量。
        return 0;

    case QUEST_STEP_ACTION:
        /* service_event 型由系统事件推进，无交互入口语义。 */
        step_action = stringp(step["action"]) ? step["action"] : "";
        if (step_action == "" || step["service_event"])
            return 0;
        cmd = stringp(step["command"]) ? step["command"] : "";
        if (cmd == "") {
            cmd = "quest action " + step_action;
            if (target != "")
                cmd += " " + target;
        }
        if (label == "")
            label = "处理";
        kind = "action";
        break;

    default:
        return 0;
    }

    action = ([
        "label": label,
        "cmd": cmd,
        "kind": kind,
        "surface": "root",
        "role": "primary",
        "target_group": "quest",
        /* 仅当前步骤可用的独立动作；固定交物入口不生成此标记。 */
        "guided": 1,
    ]);
    return action;
}

/* 进度弹窗入口：单任务当前步的动作（原 quest.c 私有实现语义 + action_label 增强）。 */
mapping *query_quest_progress_actions(object player, string quest_id, string provider_id)
{
    mapping quest_info;
    mapping step;
    mapping action;
    mixed *steps;
    mapping *actions;
    int index;

    actions = ({ });
    if (!objectp(player) || !stringp(quest_id) || quest_id == "")
        return actions;
    quest_info = query_player_quest_runtime(player, quest_id);
    if (!mapp(quest_info) || quest_info["status"] != QUEST_STATUS_ACCEPTED)
        return actions;
    steps = quest_info["steps"];
    index = quest_info["current_step"] || 0;
    if (!arrayp(steps) || index < 0 || index >= sizeof(steps))
        return actions;
    step = steps[index];
    if (!mapp(step) || step["completed"])
        return actions;
    action = build_step_guidance_action(step, provider_id != "" ? provider_id : 0);
    if (mapp(action))
        actions += ({ action });
    return actions;
}

/* NPC 交互面板入口：遍历玩家 ACCEPTED 任务，current step 指向该实体时产出指引动作。
 * match_ids 为该 NPC 可解析 id 全集（primary/alias/template/provider），由调用方组装。 */
varargs mapping *query_provider_step_actions(object player, string *match_ids)
{
    mapping data;
    mapping quest_info;
    mapping definition;
    mapping step;
    mixed *steps;
    mixed *actions;
    string quest_id;
    int index;

    actions = ({ });
    if (!objectp(player) || !arrayp(match_ids) || sizeof(match_ids) == 0)
        return actions;
    data = ensure_player_data(player);
    if (!mapp(data) || !mapp(data["active"]))
        return actions;
    foreach (quest_id in keys(data["active"])) {
        quest_info = data["active"][quest_id];
        if (!mapp(quest_info) || (quest_info["status"] || 0) != QUEST_STATUS_ACCEPTED)
            continue;
        steps = quest_info["steps"];
        index = quest_info["current_step"] || 0;
        if (!arrayp(steps) || index < 0 || index >= sizeof(steps))
            continue;
        step = steps[index];
        if (!mapp(step) || step["completed"])
            continue;
        mapping guidance_action;
        string fallback_provider;
        definition = _quest_definitions[quest_id];
        fallback_provider = mapp(definition) && stringp(definition["provider"])
            ? definition["provider"] : "";
        /* 门控：有 target 的步骤必须命中本实体 id 集；只有 TALK/ASK
         * 才能把 provider 当作无 target 的交互对象。COLLECT/GIVE/LOOK 等
         * 步骤没有实体 target 时不得挂到 NPC 面板，否则会把 get 误发给 NPC。 */
        if (stringp(step["target"]) && step["target"] != "") {
            if (member_array(step["target"], match_ids) == -1)
                continue;
        } else {
            if (step["type"] != QUEST_STEP_TALK && step["type"] != QUEST_STEP_ASK)
                continue;
            if (fallback_provider == "" || member_array(fallback_provider, match_ids) == -1)
                continue;
        }
        guidance_action = build_step_guidance_action(step,
            fallback_provider != "" ? fallback_provider : 0);
        if (mapp(guidance_action))
            actions += ({ guidance_action });
    }
    return actions;
}

mapping query_map_marker_quest_ids_by_provider(object player)
{
    mapping cache_entry;
    mapping data;
    mapping definitions;
    mapping result;
    mapping quest_def;
    mapping quest_info;
    string *quest_ids;
    string quest_id;
    string provider_id;
    string projection_token;
    int status;

    if (!objectp(player))
        return ([]);

    projection_token = sprintf("%d:%d", query_quest_definition_generation(),
        query_player_map_projection_generation(player));
    cache_entry = player->query_temp("quest/map_marker_projection_cache");
    if (mapp(cache_entry) && cache_entry["token"] == projection_token
        && mapp(cache_entry["quest_ids_by_provider"])) {
        return copy_string_array_mapping(cache_entry["quest_ids_by_provider"]);
    }

    data = ensure_player_data(player);
    if (!mapp(data))
        return ([]);

    result = ([]);
    definitions = query_visible_quest_definitions(player);
    quest_ids = sort_array(keys(definitions || ([])), 1);
    foreach (quest_id in quest_ids) {
        quest_def = definitions[quest_id];
        if (!mapp(quest_def))
            continue;

        provider_id = stringp(quest_def["provider"]) ? quest_def["provider"] : "";
        if (provider_id == "")
            continue;

        quest_info = call_other(QUEST_QUERY_HELPER, "query_player_quest_record",
            data, quest_id);
        status = mapp(quest_info)
            ? (quest_info["status"] || QUEST_STATUS_AVAILABLE)
            : QUEST_STATUS_AVAILABLE;
        switch (status) {
        case QUEST_STATUS_AVAILABLE:
            if (!call_other(QUEST_VALIDATE_HELPER, "is_quest_visible_for_player",
                definitions, data, player, quest_id))
                continue;
            break;
        case QUEST_STATUS_COMPLETED:
            break;
        case QUEST_STATUS_FAILED:
            if (!mapp(quest_info) || !quest_info["recoverable"])
                continue;
            break;
        case QUEST_STATUS_ACCEPTED:
            if (!is_map_marker_provider_step_actionable(quest_info, provider_id))
                continue;
            break;
        default:
            continue;
        }

        if (arrayp(result[provider_id]))
            result[provider_id] += ({ quest_id });
        else
            result[provider_id] = ({ quest_id });
    }
    player->set_temp("quest/map_marker_projection_cache", ([
        "token": projection_token,
        "quest_ids_by_provider": copy_string_array_mapping(result),
    ]));
    return copy_string_array_mapping(result);
}

private int is_map_marker_provider_step_actionable(mapping quest_info, string provider_id)
{
    mixed *steps;
    mapping step_state;
    string step_type;
    string target_id;
    int step_index;

    if (!mapp(quest_info) || !stringp(provider_id) || provider_id == "")
        return 0;
    if ((quest_info["status"] || 0) != QUEST_STATUS_ACCEPTED)
        return 0;

    steps = quest_info["steps"];
    step_index = intp(quest_info["current_step"]) ? quest_info["current_step"] : -1;
    if (!arrayp(steps) || step_index < 0 || step_index >= sizeof(steps)
        || !mapp(steps[step_index]))
        return 0;

    step_state = steps[step_index];
    if (step_state["completed"])
        return 0;

    step_type = stringp(step_state["type"]) ? step_state["type"] : "";
    if (!is_interactive_step_type(step_type))
        return 0;

    target_id = stringp(step_state["target"]) ? step_state["target"] : "";
    return target_id == provider_id;
}

/* 交互类步骤类型单一谓词（TALK/ASK/GIVE/ACTION 产生
 * 指引按钮并随进度变化，KILL/COLLECT/ARRIVE/LOOK/ESCORT 不在此列。
 * 所有门控（房间快照重发、provider 可行动判定）必须复用本谓词，
 * 禁止另抄 member_array 集合。 */
private int is_interactive_step_type(string step_type)
{
    return member_array(step_type, ({
        QUEST_STEP_TALK,
        QUEST_STEP_ASK,
        QUEST_STEP_GIVE,
        QUEST_STEP_ACTION,
    })) != -1;
}

/* 保留的公开查询：当前无生产调用，但被任务契约测试用作 quest_d 公开面切片锚点，
 * 也是 QUEST_CATALOG_HELPER 的对外入口——删除会同时破坏两处契约，故显式保留。 */
int quest_exists(string quest_id)
{
    return call_other(QUEST_CATALOG_HELPER, "quest_exists", _quest_definitions, quest_id);
}

int quest_exists_for_player(object player, string quest_id)
{
    return mapp(query_visible_quest_definition(player, quest_id));
}

int is_quest_completed(object player, string quest_id)
{
    if (!mapp(query_visible_quest_definition(player, quest_id)))
        return 0;
    return call_other(QUEST_QUERY_HELPER, "is_quest_completed",
        ensure_player_data(player), quest_id);
}

mapping query_player_quests_by_status(object player, string status)
{
    return call_other(QUEST_QUERY_HELPER, "query_player_quests_by_status",
        ensure_player_data(player), query_visible_quest_definitions(player), status);
}

int get_quest_status(object player, string quest_id)
{
    if (!mapp(query_visible_quest_definition(player, quest_id)))
        return 0;
    return call_other(QUEST_QUERY_HELPER, "get_quest_status",
        ensure_player_data(player), quest_id);
}

string *query_active_quests(object player)
{
    return call_other(QUEST_QUERY_HELPER, "query_active_quests",
        ensure_player_data(player), query_visible_quest_definitions(player));
}
