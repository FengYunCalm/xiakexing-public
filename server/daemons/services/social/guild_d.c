/**
 * @file    guild_d.c
 * @brief   帮派服务真源



 */

#pragma strict_types
#pragma save_types

#include <system/ansi.h>
#include <system/constants.h>
#include <system/services.h>

#undef is_player

inherit "/lib/framework/daemon";

#define DEFAULT_GUILD_SALARY 5

int save_guild_data_with_phases();

private int _save_receipt_pending;
private string _save_retry_token;
private mapping _last_guild_save_phase;
/* Existing invalid guild files are not a first boot.  Keep the owner
 * read-only until an operator restores the file; never overwrite it with an
 * empty guild map. */
nosave int _guild_state_status;
nosave string _guild_state_error;

private mapping build_command_result(int success, string message, mapping extra)
{
    mapping result;

    result = ([
        "success": success ? 1 : 0,
        "message": stringp(message) ? message : "",
    ]);
    if (mapp(extra))
        result += extra;
    return result;
}

/* Pending player state is a recovery boundary: a missing save method or a
 * failed save must never be reported as durable progress. */
private mapping persist_player_data_phase(object player)
{
    mixed err;
    mapping phase;

    if (!objectp(player) || !function_exists("save_phases", player))
        return ([ "ok": 0, "persisted": 0, "outcome": "not_written",
            "save_result": 0, "error": "player save phases unavailable" ]);
    err = catch(phase = player->save_phases());
    if (err || !mapp(phase))
        return ([ "ok": 0, "persisted": -1, "outcome": "unknown",
            "save_result": 0, "error": err ? sprintf("%O", err)
                : "save phase unavailable" ]);
    return phase;
}

private int persist_player_data(object player)
{
    mapping phase;

    phase = persist_player_data_phase(player);
    return mapp(phase) && phase["outcome"] == "committed";
}

private int commit_player_state(object player, mapping changes,
    string *deletes, string operation)
{
    mapping response;
    mapping mutation;

    if (!objectp(player) || !mapp(changes) || !arrayp(deletes))
        return 0;
    mutation = ([ "changes": changes, "deletes": deletes ]);
    response = PLAYER_STATE_D->commit_state_mutations(player, changes, deletes,
        PLAYER_STATE_D->build_state_idempotency_key(player,
            operation || "guild/player_state", mutation));
    return mapp(response) && response["ok"];
}

// Guild state is owned here. `social_d.c` may route callers into this daemon,
// but it does not duplicate membership or contribution truth.
mapping _guilds;
mapping _player_guild;
mapping _player_projection_pending;

private string get_guild_save_file()
{
    return DATA_DIR + "runtime/guild_d";
}

mapping query_guild_owner_route(string key)
{
    mapping route;
    string route_key;
    mixed err;

    route_key = stringp(key) && key != "" ? key : "guild";
    err = catch(route = OWNER_RUNTIME_D->query_service_owner_route("guild", route_key));
    if (err || !mapp(route) || route["ok"] != 1)
        return ([
            "ok": 0,
            "service": "guild",
            "key": route_key,
            "failure_reason": err ? sprintf("owner route query failed: %O", err)
                : (mapp(route) ? route["failure_reason"] : "owner route unavailable"),
            "readiness": mapp(route) ? route["readiness"] : ([ "ok": 0 ]),
            "dependencies": mapp(route) ? route["dependencies"] : ([ ]),
            "direct_cross_owner_write": 0,
        ]);

    route["commit_boundary"] = "service_owner_guild_commit";
    route["direct_cross_owner_write"] = 0;
    return route;
}

mapping query_guild_owner_status()
{
    mapping route;

    route = query_guild_owner_route("guild");
    if (!mapp(route) || route["ok"] != 1)
        return route;
    return ([
        "service_map_v2": 1,
        "service": "guild",
        "policy": route["policy"],
        "owner_id": route["owner_id"],
        "commit_boundary_required": 1,
        "commit_boundary": route["commit_boundary"],
        "service_shard_owner": route["service_shard_owner"] ? 1 : 0,
        "service_shard_key_required": route["service_shard_key_required"] ? 1 : 0,
        "keyed_service_shard_ready": route["keyed_service_shard_ready"] ? 1 : 0,
        "hot_path_service_owner_single_point": 0,
        "message_only_cross_owner": 1,
        "direct_cross_owner_write": 0,
    ]);
}

private void restore_guild_data()
{
    string file;
    mixed err;
    int restored;
    int size;

    _guild_state_status = 0;
    _guild_state_error = "";
    file = get_guild_save_file();
    size = file_size(file + ".o");
    if (size == -1) {
        _guilds = ([]);
        _player_guild = ([]);
        _player_projection_pending = ([]);
        _guild_state_status = 1;
        return;
    }
    if (size <= 0) {
        _guild_state_error = "guild state file is empty or not regular";
        _guilds = ([]);
        _player_guild = ([]);
        _player_projection_pending = ([]);
        return;
    }

    err = catch(restored = restore_object(file));
    if (err || !restored || !mapp(_guilds)) {
        _guild_state_error = err ? sprintf("%O", err)
            : !restored ? "guild state restore failed"
            : "guild state shape invalid";
        _guilds = ([]);
        _player_guild = ([]);
        _player_projection_pending = ([]);
        return;
    }

    if (!mapp(_player_guild))
        _player_guild = ([]);
    if (!mapp(_player_projection_pending))
        _player_projection_pending = ([]);
    _guild_state_status = 1;
}

private int guild_state_ready()
{
    return _guild_state_status == 1 && mapp(_guilds)
        && mapp(_player_guild) && mapp(_player_projection_pending);
}

private int persist_guild_data()
{
    /* The legacy int facade must consume the same phase owner as every
     * mutation. owner_save_object() would erase the distinction between a
     * durable file and a pending terminal receipt. */
    return save_guild_data_with_phases();
}

int save_guild_data()
{
    return persist_guild_data();
}

/* 落盘阶段详情：persisted=1 时磁盘已是新数据，调用者不得回滚内存副本。 */
private mapping persist_guild_data_phases()
{
    mapping detail;
    string file;
    string runtime_dir;

    if (!guild_state_ready()) {
        _last_guild_save_phase = ([
            "outcome": "not_written", "persisted": 0,
            "save_result": 0,
            "error": _guild_state_error || "guild state unavailable",
        ]);
        return copy(_last_guild_save_phase);
    }
    file = get_guild_save_file();
    runtime_dir = DATA_DIR + "runtime";
    if (file_size(runtime_dir) == -1)
        mkdir(runtime_dir);

    detail = owner_save_object_phases(file, "owner/global/service/guild", "guild_state", ([
        "guild_count": sizeof(_guilds),
        "player_guild_count": sizeof(_player_guild),
        "projection_pending_count": sizeof(_player_projection_pending),
        "state_digest": hash("sha256", save_variable(_guilds)
            + save_variable(_player_guild)
            + save_variable(_player_projection_pending)),
    ]), "guild/save/" + hash("sha256", save_variable(_guilds)
        + save_variable(_player_guild)
        + save_variable(_player_projection_pending)));
    _last_guild_save_phase = mapp(detail) ? copy(detail)
        : ([ "outcome": "unknown", "persisted": -1,
            "save_result": 0, "error": "guild owner save result unavailable" ]);
    _save_receipt_pending = mapp(detail) && detail["persisted"] == 1
        && detail["ok"] != 1 ? 1 : 0;
    _save_retry_token = mapp(detail) && stringp(detail["retry_token"])
        ? detail["retry_token"] : "";
    if (_save_receipt_pending)
        log_file("guild", sprintf("%s guild state persisted but terminal receipt pending (token %s)\n",
            ctime(time()), _save_retry_token));
    return detail;
}

/* 1=完整提交；0=没有落盘（调用方可回滚）；2=已落盘但收据待补（不可回滚）。 */
int save_guild_data_with_phases()
{
    mapping detail;

    detail = persist_guild_data_phases();
    if (mapp(detail) && detail["ok"] == 1)
        return 1;
    if (mapp(detail) && detail["persisted"] == 1)
        return 2;
    return 0;
}

int query_guild_save_receipt_pending()
{
    return _save_receipt_pending;
}

mapping query_guild_save_phase()
{
    return mapp(_last_guild_save_phase) ? copy(_last_guild_save_phase)
        : ([ "outcome": "not_attempted", "persisted": 0,
            "save_result": 0 ]);
}

mapping query_guild_runtime_snapshot()
{
    return ([
        "guild_state_loaded": guild_state_ready() ? 1 : 0,
        "guild_state_error": _guild_state_error || "",
        "guild_count": sizeof(_guilds || ([])),
        "player_guild_count": sizeof(_player_guild || ([])),
        "projection_pending_count": sizeof(_player_projection_pending || ([])),
    ]);
}

private mixed deep_copy_value(mixed value)
{
    mixed restored;

    if (catch(restored = restore_variable(save_variable(value))))
        return 0;
    return restored;
}

private mapping snapshot_guild_state()
{
    /* L-93：投影待恢复表是本次变更边界的一部分——主账本与投影写入同属
     * 一次 mutation，回滚必须把待恢复表一起还原，否则回滚后会留下
     * “为未发生的变更重放投影”的悬空记录。 */
    return ([
        "guilds": deep_copy_value(_guilds || ([])),
        "player_guild": deep_copy_value(_player_guild || ([])),
        "player_projection_pending": deep_copy_value(_player_projection_pending || ([])),
    ]);
}

private void restore_guild_state(mapping snapshot)
{
    _guilds = mapp(snapshot) && mapp(snapshot["guilds"])
        ? snapshot["guilds"] : ([]);
    _player_guild = mapp(snapshot) && mapp(snapshot["player_guild"])
        ? snapshot["player_guild"] : ([]);
    _player_projection_pending = mapp(snapshot)
        && mapp(snapshot["player_projection_pending"])
            ? snapshot["player_projection_pending"] : ([]);
}

private int commit_guild_state(mapping snapshot)
{
    int outcome;

    outcome = save_guild_data_with_phases();
    if (outcome == 1)
        return 1;
    /* persisted_receipt_pending/unknown keep the candidate in memory.  Only
     * an explicit not_written result permits restoring the old guild state. */
    if (mapp(_last_guild_save_phase)
        && _last_guild_save_phase["outcome"] != "not_written")
        return 0;
    restore_guild_state(snapshot);
    return 0;
}

private string normalize_guild_id(string name)
{
    if (!stringp(name) || name == "")
        return 0;

    return lower_case(replace_string(name, " ", "_"));
}

private string query_rank_label(string rank)
{
    switch (rank) {
        case "leader":
            return "帮主";
        case "deputy":
            return "副帮主";
        case "elder":
            return "长老";
        case "chef":
            return "堂主";
        case "elite":
            return "精英";
        case "member":
            return "帮众";
        case "trial":
            return "见习";
    }

    return "未知";
}

private string ensure_player_id(object player)
{
    string player_id;

    if (!objectp(player))
        return 0;

    player_id = get_object_id(player);
    if (!stringp(player_id) || player_id == "")
        return 0;

    return player_id;
}

private mapping ensure_guild(string guild_id)
{
    if (!stringp(guild_id) || !mapp(_guilds[guild_id]))
        return 0;
    return _guilds[guild_id];
}

private mapping ensure_member(mapping guild, string member_id)
{
    if (!mapp(guild) || !stringp(member_id) || member_id == "")
        return 0;
    if (!mapp(guild["members"]))
        return 0;
    if (!mapp(guild["members"][member_id]))
        return 0;
    return guild["members"][member_id];
}

private int query_guild_member_limit(mapping guild)
{
    int max_members;

    if (!mapp(guild))
        return MAX_GUILD_SIZE;

    max_members = guild["max_members"];
    if (!intp(max_members) || max_members <= 0)
        max_members = MAX_GUILD_SIZE;

    return max_members;
}

private int can_join_guild(object player, string guild_id, mapping guild)
{
    mapping requirements;
    int required_skill_cap;
    int player_reputation;
    int required_reputation;
    string required_quest;
    string required_gender;
    string player_gender;

    if (!objectp(player) || !stringp(guild_id) || !mapp(guild))
        return 0;

    if (!mapp(guild["members"]))
        return 0;

    if (sizeof(keys(guild["members"])) >= query_guild_member_limit(guild))
        return 0;

    requirements = guild["requirements"];
    if (!mapp(requirements) || sizeof(requirements) == 0)
        return 1;

    required_skill_cap = requirements["skill_cap"];
    if (intp(required_skill_cap) && required_skill_cap > 0) {
        if (!NUMERIC_RULE_D->player_meets_skill_cap_gate(player, required_skill_cap))
            return 0;
    }

    required_reputation = requirements["reputation"];
    if (intp(required_reputation) && required_reputation > 0) {
        player_reputation = REPUTATION_D->query_reputation_scope(player, guild_id);
        if (player_reputation < required_reputation)
            return 0;
    }

    required_quest = requirements["quest"];
    if (stringp(required_quest) && required_quest != "") {
        if (!QUEST_D->is_quest_completed(player, required_quest))
            return 0;
    }

    required_gender = requirements["gender"];
    if (stringp(required_gender) && required_gender != "") {
        player_gender = player->query("gender") || "男性";
        if (player_gender != required_gender)
            return 0;
    }

    return 1;
}

private string select_next_leader(mapping guild, string leaving_member_id)
{
    string *member_ids;
    string member_id;
    string next_leader_id;
    mapping member;
    int next_joined_at;
    int member_joined_at;

    if (!mapp(guild) || !mapp(guild["members"]))
        return 0;

    member_ids = keys(guild["members"]);
    foreach (member_id in member_ids) {
        if (member_id == leaving_member_id)
            continue;

        member = ensure_member(guild, member_id);
        if (!mapp(member))
            continue;

        member_joined_at = intp(member["joined_at"]) ? member["joined_at"] : 0;
        if (!stringp(next_leader_id)) {
            next_leader_id = member_id;
            next_joined_at = member_joined_at;
            continue;
        }

        if (member_joined_at < next_joined_at ||
            (member_joined_at == next_joined_at && member_id < next_leader_id)) {
            next_leader_id = member_id;
            next_joined_at = member_joined_at;
        }
    }

    return next_leader_id;
}

private mapping snapshot_player_projection(object player)
{
    mixed guild;
    mixed rank;
    mixed contribution;

    guild = player->query("guild");
    rank = player->query("guild_rank");
    contribution = player->query("guild_contribution");
    return ([
        "guild_present": !undefinedp(guild),
        "guild": guild,
        "rank_present": !undefinedp(rank),
        "rank": rank,
        "contribution_present": !undefinedp(contribution),
        "contribution": contribution,
    ]);
}

private int restore_player_projection(object player, mapping snapshot)
{
    mapping changes;
    string *deletes;

    if (!objectp(player) || !mapp(snapshot))
        return 0;
    changes = ([ ]);
    deletes = ({ });
    if (snapshot["guild_present"])
        changes["guild"] = snapshot["guild"];
    else
        deletes += ({ "guild" });
    if (snapshot["rank_present"])
        changes["guild_rank"] = snapshot["rank"];
    else
        deletes += ({ "guild_rank" });
    if (snapshot["contribution_present"])
        changes["guild_contribution"] = snapshot["contribution"];
    else
        deletes += ({ "guild_contribution" });
    return commit_player_state(player, changes, deletes,
        "guild/projection_rollback");
}

private mapping build_player_projection(string player_id)
{
    string guild_id;
    mapping guild;
    mapping member;

    guild_id = stringp(player_id) ? (_player_guild[player_id] || "") : "";
    guild = ensure_guild(guild_id);
    member = ensure_member(guild, player_id);
    if (!mapp(guild) || !mapp(member))
        return ([ "joined": 0 ]);

    return ([
        "joined": 1,
        "guild": guild_id,
        "rank": member["rank"] || "member",
        "contribution": member["contribution"] || 0,
    ]);
}

private int apply_player_projection(object player, mapping projection)
{
    mapping changes;

    if (!objectp(player))
        return 0;
    if (!mapp(projection) || !projection["joined"]) {
        return commit_player_state(player, ([ ]),
            ({ "guild", "guild_rank", "guild_contribution" }),
            "guild/projection_clear");
    }

    changes = ([
        "guild": projection["guild"] || "",
        "guild_rank": projection["rank"] || "member",
        "guild_contribution": projection["contribution"] || 0,
    ]);
    return commit_player_state(player, changes, ({ }), "guild/projection_apply");
}

private int persist_pending_player_projection(string player_id, mapping projection)
{
    mapping pending;
    int save_result;

    if (!stringp(player_id) || player_id == "" || !mapp(projection))
        return 0;

    pending = projection + ([
        /* operation identity follows the frozen projection, not wall-clock
         * seconds; the same candidate retries the same operation. */
        "operation_id": "guild_projection/" + player_id + "/"
            + hash("sha256", save_variable(projection)),
        "created_at": time(),
    ]);
    _player_projection_pending[player_id] = pending;
    save_result = save_guild_data_with_phases();
    if (save_result != 1) {
        log_file("guild", sprintf("%s player projection pending save incomplete for %s: %O\\n",
            ctime(time()), player_id, query_guild_save_phase()));
        return 0;
    }
    return 1;
}

mapping sync_player_cache(object player)
{
    string player_id;
    mapping projection;
    mapping previous;
    mapping snapshot;
    mapping save_phase;

    if (!objectp(player))
        return ([ "ok": 0, "pending_recovery": 0, "error": "invalid_player" ]);

    player_id = ensure_player_id(player);
    if (!stringp(player_id))
        return ([ "ok": 0, "pending_recovery": 0, "error": "missing_player_id" ]);

    projection = build_player_projection(player_id);
    previous = snapshot_player_projection(player);
    if (!apply_player_projection(player, projection))
        return ([ "ok": 0, "pending_recovery": 0,
            "error": "player_state_commit_failed" ]);
    save_phase = persist_player_data_phase(player);
    if (mapp(save_phase) && save_phase["outcome"] == "committed") {
        if (mapp(_player_projection_pending[player_id])) {
            snapshot = snapshot_guild_state();
            map_delete(_player_projection_pending, player_id);
            if (!commit_guild_state(snapshot))
                return ([ "ok": 1, "pending_recovery": 1,
                    "error": "pending_clear_failed" ]);
        }
        return ([ "ok": 1, "pending_recovery": 0,
            "persisted": 1, "save_result": save_phase["save_result"] ]);
    }

    /* 已落盘但收据未闭合或磁盘证据未知时，不能恢复旧投影。把当前
     * projection 保留为 guild owner 的 pending 记录，等待原收据恢复。 */
    if (mapp(save_phase)
        && (save_phase["outcome"] == "persisted_receipt_pending"
            || save_phase["outcome"] == "unknown")) {
        if (!persist_pending_player_projection(player_id, projection))
            log_file("guild", sprintf("%s player projection receipt pending for %s\\n",
                ctime(time()), player_id));
        return ([ "ok": 0, "pending_recovery": 1,
            "persisted": save_phase["persisted"],
            "save_result": save_phase["save_result"],
            "state": save_phase["outcome"],
            "error": save_phase["error"] || "player receipt pending" ]);
    }

    if (!restore_player_projection(player, previous))
        return ([ "ok": 0, "pending_recovery": 1,
            "error": "player_projection_rollback_failed" ]);
    if (!persist_pending_player_projection(player_id, projection))
        log_file("guild", sprintf("%s player projection recovery is memory-only for %s\\n",
            ctime(time()), player_id));
    return ([ "ok": 1, "pending_recovery": 1,
        "error": "player_persistence_failed" ]);
}

mapping recover_pending_player_projection(object player)
{
    string player_id;
    mapping pending;
    mapping snapshot;
    mapping save_phase;

    if (!objectp(player))
        return ([ "ok": 0, "recovered": 0, "pending": 0,
            "error": "invalid_player" ]);

    player_id = ensure_player_id(player);
    pending = stringp(player_id) ? _player_projection_pending[player_id] : 0;
    if (!mapp(pending))
        return ([ "ok": 1, "recovered": 0, "pending": 0 ]);

    if (!apply_player_projection(player, pending))
        return ([ "ok": 0, "recovered": 0, "pending": 1,
            "error": "player_state_commit_failed" ]);
    save_phase = persist_player_data_phase(player);
    if (!mapp(save_phase) || save_phase["outcome"] != "committed")
        return ([ "ok": 0, "recovered": 0, "pending": 1,
            "persisted": mapp(save_phase) ? save_phase["persisted"] : -1,
            "save_result": mapp(save_phase) ? save_phase["save_result"] : 0,
            "state": mapp(save_phase) ? save_phase["outcome"] : "unknown",
            "error": mapp(save_phase) && save_phase["error"]
                ? save_phase["error"] : "player_persistence_failed" ]);

    snapshot = snapshot_guild_state();
    map_delete(_player_projection_pending, player_id);
    if (!commit_guild_state(snapshot))
        return ([ "ok": 0, "recovered": 0, "pending": 1,
            "error": "pending_clear_failed" ]);

    return ([ "ok": 1, "recovered": 1, "pending": 0,
        "operation_id": pending["operation_id"] ]);
}


void create()
{
    ::create();
    restore_guild_data();
    /* 登录恢复经 player_lifecycle manifest 装配（唯一真源），
     * 本 daemon 不再 create 时注册。 */
}

int guild_exists(string guild_id)
{
    return mapp(_guilds[guild_id]);
}

mapping query_guild(string guild_id)
{
    if (!stringp(guild_id) || !mapp(_guilds[guild_id]))
        return 0;

    return copy(_guilds[guild_id]);
}

string *query_all_guilds()
{
    return keys(_guilds || ([]));
}

mapping query_guild_info(string guild_id)
{
    return query_guild(guild_id);
}

string query_player_guild(object player)
{
    string player_id;

    if (!objectp(player))
        return 0;

    player_id = ensure_player_id(player);
    if (!stringp(player_id) || player_id == "")
        return 0;

    return _player_guild[player_id];
}

mapping query_player_guild_status(object player)
{
    string guild_id;
    string player_id;
    mapping guild;
    mapping member;

    if (!objectp(player))
        return ([ "joined": 0 ]);

    player_id = ensure_player_id(player);
    guild_id = stringp(player_id) ? (_player_guild[player_id] || "") : "";
    guild = ensure_guild(guild_id);
    member = ensure_member(guild, player_id);
    if (!mapp(guild) || !mapp(member))
        return ([ "joined": 0 ]);

    return ([
        "joined": 1,
        "guild_id": guild_id,
        "guild_name": stringp(guild["name"]) ? guild["name"] : guild_id,
        "rank_key": member["rank"] || "member",
        "contribution": member["contribution"] || 0,
    ]);
}

mapping query_player_guild_skill_catalog(object player)
{
    mapping status;

    status = query_player_guild_status(player);
    if (!mapp(status) || !status["joined"])
        return ([]);

    return GUILD_SKILL_D->query_guild_skill_catalog(status["guild_id"] || "");
}

mapping query_guild_skill_definition(string guild_id, string guild_skill_id)
{
    return GUILD_SKILL_D->query_guild_skill_definition(guild_id, guild_skill_id);
}

mapping apply_contribution_event(object player, int amount, string source, string reason)
{
    string guild_id;
    string player_id;
    mapping guild;
    mapping member;
    mapping snapshot;
    int applied_amount;
    int current_contribution;
    int next_contribution;

    if (!objectp(player) || !intp(amount) || amount == 0)
        return ([ "ok": 0, "error": "invalid_arguments" ]);

    player_id = ensure_player_id(player);
    guild_id = stringp(player_id) ? (_player_guild[player_id] || "") : "";
    guild = ensure_guild(guild_id);
    member = ensure_member(guild, player_id);
    if (!mapp(guild) || !mapp(member))
        return ([ "ok": 0, "error": "not_in_guild" ]);

    current_contribution = member["contribution"] || 0;
    next_contribution = current_contribution + amount;
    if (next_contribution < 0) {
        return ([
            "ok": 0,
            "error": "insufficient_contribution",
            "contribution": current_contribution,
            "applied_amount": 0,
            "source": stringp(source) ? source : "",
            "reason": stringp(reason) ? reason : "",
        ]);
    }

    snapshot = snapshot_guild_state();
    applied_amount = next_contribution - current_contribution;
    member["contribution"] = next_contribution;
    if (applied_amount > 0)
        member["total_contribution"] = (member["total_contribution"] || 0) + applied_amount;
    if (!commit_guild_state(snapshot))
        return ([
            "ok": 0,
            "error": "persistence_failed",
            "contribution": current_contribution,
            "applied_amount": 0,
            "source": stringp(source) ? source : "",
            "reason": stringp(reason) ? reason : "",
        ]);

    sync_player_cache(player);
    return ([
        "ok": 1,
        "guild_id": guild_id,
        "contribution": next_contribution,
        "applied_amount": applied_amount,
        "source": stringp(source) ? source : "",
        "reason": stringp(reason) ? reason : "",
    ]);
}

string *query_guild_members(string guild_id)
{
    mapping guild;

    if (!stringp(guild_id))
        return ({});

    guild = _guilds[guild_id];
    if (!mapp(guild) || !mapp(guild["members"]))
        return ({});

    return keys(guild["members"]);
}

int create_guild(object founder, string name)
{
    string guild_id;
    string player_id;
    mapping snapshot;

    if (!objectp(founder) || !stringp(name) || name == "")
        return 0;

    player_id = ensure_player_id(founder);
    if (!stringp(player_id))
        return 0;

    if (stringp(_player_guild[player_id]))
        return 0;

    guild_id = normalize_guild_id(name);
    if (!stringp(guild_id) || guild_exists(guild_id))
        return 0;

    snapshot = snapshot_guild_state();
    _guilds[guild_id] = ([
        "id": guild_id,
        "name": name,
        "founder": player_id,
        "members": ([ player_id: ([
            "rank": "leader",
            "contribution": 0,
            "total_contribution": 0,
            "salary_last": 0,
            "joined_at": time()
        ]) ]),
        "created_at": time(),
        "level": 1,
        "type": "guild",
        "salary": DEFAULT_GUILD_SALARY,
        "max_members": MAX_GUILD_SIZE,
        "requirements": ([])
    ]);

    _player_guild[player_id] = guild_id;
    if (!commit_guild_state(snapshot))
        return 0;

    sync_player_cache(founder);
    msg_send(founder, HIG "【帮派】你创建了帮派：" + name + NOR "\n");
    return 1;
}

int disband_guild(string guild_id)
{
    mapping guild;
    mapping snapshot;
    string *member_ids;
    string member_id;
    object member;

    guild = _guilds[guild_id];
    if (!mapp(guild))
        return 0;

    snapshot = snapshot_guild_state();
    member_ids = keys(guild["members"] || ([]));
    foreach (member_id in member_ids)
        map_delete(_player_guild, member_id);
    map_delete(_guilds, guild_id);

    if (!commit_guild_state(snapshot))
        return 0;

    foreach (member_id in member_ids) {
        member = find_player_online(member_id);
        if (objectp(member))
            sync_player_cache(member);
    }
    return 1;
}

int join_guild(object player, string guild_id)
{
    mapping guild;
    mapping snapshot;
    string player_id;
    string guild_name;

    if (!objectp(player) || !stringp(guild_id))
        return 0;

    guild = _guilds[guild_id];
    player_id = ensure_player_id(player);
    if (!mapp(guild) || !stringp(player_id) || stringp(_player_guild[player_id]))
        return 0;

    if (!can_join_guild(player, guild_id, guild))
        return 0;

    snapshot = snapshot_guild_state();
    guild["members"][player_id] = ([
        "rank": "member",
        "contribution": 0,
        "total_contribution": 0,
        "salary_last": 0,
        "joined_at": time()
    ]);
    _player_guild[player_id] = guild_id;
    guild_name = stringp(guild["name"]) ? guild["name"] : guild_id;
    if (!commit_guild_state(snapshot))
        return 0;

    sync_player_cache(player);
    msg_send(player, HIG "【帮派】你加入了帮派：" + guild_name + NOR "\n");
    return 1;
}

int leave_guild(object player)
{
    string player_id;
    string guild_id;
    string next_leader_id;
    mapping guild;
    mapping member;
    mapping next_leader;
    mapping snapshot;
    object successor;
    int is_leader;

    if (!objectp(player))
        return 0;

    player_id = ensure_player_id(player);
    guild_id = _player_guild[player_id];
    guild = ensure_guild(guild_id);
    if (!stringp(player_id) || !stringp(guild_id) || !mapp(guild))
        return 0;

    member = ensure_member(guild, player_id);
    if (!mapp(member))
        return 0;

    snapshot = snapshot_guild_state();
    is_leader = (member["rank"] == "leader");
    if (is_leader)
        next_leader_id = select_next_leader(guild, player_id);

    map_delete(guild["members"], player_id);
    map_delete(_player_guild, player_id);

    if (is_leader) {
        if (!stringp(next_leader_id)) {
            map_delete(_guilds, guild_id);
        } else {
            next_leader = ensure_member(guild, next_leader_id);
            if (!mapp(next_leader)) {
                map_delete(_guilds, guild_id);
            } else {
                next_leader["rank"] = "leader";
                if (guild["founder"] == player_id)
                    guild["founder"] = next_leader_id;
            }
        }
    }

    if (!commit_guild_state(snapshot))
        return 0;

    sync_player_cache(player);
    if (stringp(next_leader_id)) {
        successor = find_player_online(next_leader_id);
        if (objectp(successor))
            sync_player_cache(successor);
    }
    msg_send(player, HIG "【帮派】你已退出帮派。" NOR "\n");
    return 1;
}

int add_contribution(object player, int amount, string reason)
{
    string player_id;
    string guild_id;
    mapping guild;
    mapping member;
    mapping snapshot;

    if (!objectp(player) || amount <= 0)
        return 0;

    player_id = get_object_id(player);
    guild_id = _player_guild[player_id];
    guild = _guilds[guild_id];
    if (!stringp(player_id) || !stringp(guild_id) || !mapp(guild))
        return 0;

    member = guild["members"][player_id];
    if (!mapp(member))
        return 0;

    snapshot = snapshot_guild_state();
    member["contribution"] = member["contribution"] + amount;
    member["total_contribution"] = member["total_contribution"] + amount;
    if (!commit_guild_state(snapshot))
        return 0;

    sync_player_cache(player);
    msg_send(player,
        sprintf(HIG "【帮派】你获得了 %d 点贡献度（%s）。" NOR "\n", amount, reason || "帮派贡献"));
    return 1;
}

int consume_contribution(object player, int amount)
{
    string player_id;
    string guild_id;
    mapping guild;
    mapping member;
    mapping snapshot;

    if (!objectp(player) || amount <= 0)
        return 0;

    player_id = ensure_player_id(player);
    guild_id = _player_guild[player_id];
    guild = ensure_guild(guild_id);
    if (!stringp(player_id) || !mapp(guild))
        return 0;

    member = ensure_member(guild, player_id);
    if (!mapp(member) || member["contribution"] < amount)
        return 0;

    snapshot = snapshot_guild_state();
    member["contribution"] -= amount;
    if (!commit_guild_state(snapshot))
        return 0;

    sync_player_cache(player);
    return 1;
}

int query_member_join_time(object player)
{
    string player_id;
    string guild_id;
    mapping guild;
    mapping member;

    if (!objectp(player))
        return 0;

    player_id = ensure_player_id(player);
    guild_id = _player_guild[player_id];
    guild = ensure_guild(guild_id);
    if (!stringp(player_id) || !mapp(guild))
        return 0;

    member = ensure_member(guild, player_id);
    if (!mapp(member))
        return 0;

    return member["joined_at"] || 0;
}

int kick_member(object leader, string member_id)
{
    string leader_id;
    string guild_id;
    mapping guild;
    mapping leader_data;
    mapping snapshot;
    object member;

    leader_id = ensure_player_id(leader);
    if (!stringp(leader_id) || !stringp(member_id) || member_id == "")
        return 0;

    guild_id = _player_guild[leader_id];
    guild = ensure_guild(guild_id);
    if (!mapp(guild))
        return 0;

    leader_data = ensure_member(guild, leader_id);
    if (!mapp(leader_data) || leader_data["rank"] != "leader")
        return 0;
    if (member_id == leader_id)
        return 0;

    if (!mapp(guild["members"][member_id]))
        return 0;

    snapshot = snapshot_guild_state();
    map_delete(guild["members"], member_id);
    map_delete(_player_guild, member_id);
    if (!commit_guild_state(snapshot))
        return 0;

    member = find_player_online(member_id);
    if (objectp(member))
        sync_player_cache(member);
    return 1;
}

int appoint_member(object leader, string member_id, string rank)
{
    string leader_id;
    string guild_id;
    mapping guild;
    mapping leader_data;
    mapping member_data;
    mapping snapshot;
    object member;
    string guild_name;

    leader_id = ensure_player_id(leader);
    if (!stringp(leader_id) || !stringp(member_id) || member_id == "")
        return 0;

    if (member_array(rank, ({ "deputy", "elder", "chef", "elite", "member", "trial" })) == -1)
        return 0;

    guild_id = _player_guild[leader_id];
    guild = ensure_guild(guild_id);
    if (!mapp(guild))
        return 0;

    leader_data = ensure_member(guild, leader_id);
    if (!mapp(leader_data) || leader_data["rank"] != "leader")
        return 0;

    /* L-96：会长不能通过任命把自己降级——帮派必须始终有唯一会长。
     * 移交会长走专门的移交入口，不借道成员任命。 */
    if (member_id == leader_id) {
        msg_send(leader, HIR "【帮派】不能任命自己，会长移交请使用移交指令。" NOR "\n");
        return 0;
    }

    member_data = ensure_member(guild, member_id);
    if (!mapp(member_data))
        return 0;

    snapshot = snapshot_guild_state();
    member_data["rank"] = rank;
    guild_name = stringp(guild["name"]) ? guild["name"] : guild_id;
    if (!commit_guild_state(snapshot))
        return 0;

    member = find_player_online(member_id);
    if (objectp(member))
        sync_player_cache(member);
    msg_send(leader, HIG "【帮派】你已将成员任命为 " + query_rank_label(rank) + "（帮派：" + guild_name + "）。" NOR "\n");
    if (objectp(member))
        msg_send(member, HIG "【帮派】你的职位已变更为 " + query_rank_label(rank) + "（帮派：" + guild_name + "）。" NOR "\n");
    return 1;
}

int transfer_leadership(object leader, string member_id)
{
    string leader_id;
    string guild_id;
    mapping guild;
    mapping leader_data;
    mapping member_data;
    mapping snapshot;
    object member;

    leader_id = ensure_player_id(leader);
    if (!stringp(leader_id) || !stringp(member_id) || member_id == "")
        return 0;

    guild_id = _player_guild[leader_id];
    guild = ensure_guild(guild_id);
    if (!mapp(guild))
        return 0;

    leader_data = ensure_member(guild, leader_id);
    if (!mapp(leader_data) || leader_data["rank"] != "leader")
        return 0;

    member_data = ensure_member(guild, member_id);
    if (!mapp(member_data))
        return 0;

    snapshot = snapshot_guild_state();
    leader_data["rank"] = "member";
    member_data["rank"] = "leader";
    if (!commit_guild_state(snapshot))
        return 0;

    member = find_player_online(member_id);
    if (objectp(member))
        sync_player_cache(member);
    sync_player_cache(leader);
    return 1;
}

/* 捐赠意图了结标记：业务腿成功，或失败后已由即时退款/退款挂账接管。
 * 恢复消费者对 completed 只清账不退款，needs_review 才留给真正无法
 * 归因的崩溃窗口。 */
private void mark_donation_intent_completed(object player, string operation_id)
{
    mapping pending;

    if (!objectp(player) || !stringp(operation_id) || operation_id == "")
        return;
    pending = player->query("economy/guild_refund_pending");
    if (!mapp(pending) || !mapp(pending[operation_id]))
        return;
    pending[operation_id]["state"] = "completed";
    pending[operation_id]["completed_at"] = time();
    if (!commit_player_state(player,
        ([ "economy/guild_refund_pending": pending ]), ({ }),
        "guild/donation_intent_complete"))
        log_file("guild", sprintf(
            "%s donation intent complete marker save failed for %s (op=%s)\n",
            ctime(time()), ensure_player_id(player) || "?", operation_id));
}

/* 唯一 recovery owner——donate/login/admin 都调本函数，admin 不复制
 * 资金逻辑。幂等：refund 带 operation_id（MONEY_D receipt），只有
 * committed/duplicate 才删挂账；崩溃重放不会重复加钱。返回结构化状态，
 * 让生命周期健康快照暴露仍待处理的退款。 */
mapping recover_pending_guild_refunds(object player)
{
    mapping entry;
    mapping pending;
    mapping receipt;
    mapping save_phase;
    string *sorted_keys;
    string pending_key;
    string operation_id;
    int recovered;
    int all_ok;

    if (!objectp(player))
        return ([ "ok": 0, "recovered": 0, "pending": 0,
            "error": "invalid player" ]);
    pending = player->query("economy/guild_refund_pending");
    if (!mapp(pending) || sizeof(pending) == 0)
        return ([ "ok": 1, "recovered": 0, "pending": 0 ]);
    all_ok = 1;
    sorted_keys = sort_array(keys(pending), "sort_pending_by_created_at",
        this_object(), pending);
    foreach (pending_key in sorted_keys) {
        entry = pending[pending_key];
        if (mapp(entry) && entry["state"] == "intent") {
            mapping money_receipt;

            /* L-95：意图阶段的资金腿还没被证明。没收据 → 扣款未发生，
             * 直接丢弃意图（不能凭空退款）；有收据 → 转为待退款处理。 */
            money_receipt = MONEY_D->query_money_operation_receipt(player,
                entry["operation_id"]);
            if (!mapp(money_receipt)) {
                map_delete(pending, pending_key);
                if (!commit_player_state(player,
                    ([ "economy/guild_refund_pending": pending ]), ({ }),
                    "guild/donation_intent_drop")) {
                    pending[pending_key] = entry;
                    all_ok = 0;
                }
                continue;
            }
            /* 资金腿已发生但业务腿结局未知（贡献是否记账没有可归因记录）。
             * 自动退款可能双倍入账，静默丢弃可能吞钱，因此标记待人工核对并
             * 保留在健康快照里——既不猜也不隐藏。 */
            entry["state"] = "needs_review";
            entry["attempts"] = (entry["attempts"] || 0) + 1;
            entry["detected_at"] = time();
            pending[pending_key] = entry;
            if (!commit_player_state(player,
                ([ "economy/guild_refund_pending": pending ]), ({ }),
                "guild/donation_intent_review")) {
                all_ok = 0;
                continue;
            }
            log_file("guild", sprintf(
                "%s donation intent needs review for %s (op=%s amount=%d)\n",
                ctime(time()), ensure_player_id(player) || "?",
                entry["operation_id"], entry["amount"] || 0));
            all_ok = 0;
            continue;
        }
        if (mapp(entry) && entry["state"] == "completed") {
            map_delete(pending, pending_key);
            if (!commit_player_state(player,
                ([ "economy/guild_refund_pending": pending ]), ({ }),
                "guild/donation_intent_clear")) {
                pending[pending_key] = entry;
                all_ok = 0;
            }
            continue;
        }
        if (intp(entry) && (int)entry > 0) {
            /* 旧格式条目（历史格式 pending[key]=amount int）：生成稳定
             * operation_id 幂等恢复（不静默滞留、不重复加钱）。 */
            entry = ([
                "amount": entry,
                "operation_id": "guild_refund_legacy_" + pending_key,
                "created_at": time(),
                "attempts": 0,
                "state": "pending",
            ]);
            pending[pending_key] = entry;
        }
        if (!mapp(entry) || !intp(entry["amount"]) || entry["amount"] <= 0) {
            all_ok = 0;
            continue;
        }
        /* recovery 前 pin receipt——pending 引用的 receipt 不被
         * 128 FIFO 裁剪；成功顺序：确认 committed/duplicate -> 内存同时
         * 删除 pending/unpin -> 一次 save。 */
        operation_id = stringp(entry["operation_id"])
            ? entry["operation_id"] : "";
        if (!MONEY_D->pin_money_operation(player, operation_id, "GUILD_D")) {
            entry["attempts"] = (entry["attempts"] || 0) + 1;
            entry["state"] = "retry_failed";
            entry["last_error"] = "receipt pin owner conflict";
            pending[pending_key] = entry;
            if (!commit_player_state(player,
                ([ "economy/guild_refund_pending": pending ]), ({ }),
                "guild/refund_retry")) {
                all_ok = 0;
                continue;
            }
            all_ok = 0;
            if (!persist_player_data(player))
                log_file("guild", sprintf("%s refund pin retry save failed for %s\n",
                    ctime(time()), ensure_player_id(player) || "?"));
            continue;
        }
        receipt = ECONOMY_SETTLEMENT_D->refund_guild_donation(player,
            entry["amount"], operation_id);
        if (mapp(receipt) && receipt["ok"]) {
            /* 先同时移除挂账和 pin，再保存；两者任一保存失败都保留
             * 内存挂账并恢复 pin，下一次恢复会命中 duplicate 后重试保存。 */
            map_delete(pending, pending_key);
            if (!commit_player_state(player,
                ([ "economy/guild_refund_pending": pending ]), ({ }),
                "guild/refund_complete")) {
                pending[pending_key] = entry;
                all_ok = 0;
                continue;
            }
            if (!MONEY_D->unpin_money_operation(player,
                operation_id, "GUILD_D")) {
                pending[pending_key] = entry;
                if (!commit_player_state(player,
                    ([ "economy/guild_refund_pending": pending ]), ({ }),
                    "guild/refund_rollback"))
                    log_file("guild", sprintf("%s refund pending rollback failed for %s\n",
                        ctime(time()), ensure_player_id(player) || "?"));
                all_ok = 0;
                continue;
            }
            save_phase = persist_player_data_phase(player);
            if (!mapp(save_phase) || save_phase["outcome"] != "committed") {
                /* 只有 not_written 才能撤销“清理挂账 + 解除 pin”的候选。
                 * persisted_receipt_pending/unknown 仍保留冻结候选，由 owner
                 * 保存阶段补原收据，不能重新放回挂账并制造第二候选。
                 */
                if (mapp(save_phase)
                    && save_phase["outcome"] == "not_written") {
                    pending[pending_key] = entry;
                    if (!commit_player_state(player,
                        ([ "economy/guild_refund_pending": pending ]), ({ }),
                        "guild/refund_rollback"))
                        log_file("guild", sprintf("%s refund pending rollback failed for %s\n",
                            ctime(time()), ensure_player_id(player) || "?"));
                    MONEY_D->pin_money_operation(player, operation_id, "GUILD_D");
                }
                log_file("guild", sprintf("%s refund pending save failed for %s: %O\n",
                    ctime(time()), ensure_player_id(player) || "?",
                    save_phase));
                all_ok = 0;
                continue;
            }
            recovered++;
        } else {
            /* 退款仍失败：attempts++ 保留挂账（不阻塞后续笔），
             * 健康检查可见，下次恢复再试。 */
            entry["attempts"] = (entry["attempts"] || 0) + 1;
            entry["state"] = "retry_failed";
            entry["last_error"] = mapp(receipt) && receipt["reason"]
                ? receipt["reason"] : "refund failed";
            pending[pending_key] = entry;
            if (!commit_player_state(player,
                ([ "economy/guild_refund_pending": pending ]), ({ }),
                "guild/refund_retry")) {
                all_ok = 0;
                continue;
            }
            all_ok = 0;
            if (!persist_player_data(player))
                log_file("guild", sprintf("%s refund retry state save failed for %s\n",
                    ctime(time()), ensure_player_id(player) || "?"));
        }
    }
    return ([ "ok": all_ok, "recovered": recovered,
        "pending": mapp(pending) ? sizeof(pending) : 0 ]);
}

/* 挂账回放比较器（按 created_at 升序；不依赖 mapping 遍历序）。 */
int sort_pending_by_created_at(string a, string b, mapping pending)
{
    int ta;
    int tb;
    mapping ea;
    mapping eb;

    ea = pending[a];
    eb = pending[b];
    ta = mapp(ea) ? (intp(ea["created_at"]) ? ea["created_at"] : 0) : 0;
    tb = mapp(eb) ? (intp(eb["created_at"]) ? eb["created_at"] : 0) : 0;
    return ta < tb ? -1 : (ta > tb ? 1 : 0);
}

int donate_guild(object player, int amount)
{
    mapping pending;
    mapping created;
    mapping committed;
    mapping money_payload;
    mapping prepared;
    mapping intent_pending;
    string money_key;
    string operation_id;
    string refund_operation_id;
    mixed err;
    int seq;
    int txn_id;

    if (!objectp(player) || amount <= 0)
        return 0;

    /* 先补历史退款（唯一 recovery owner；幂等，不重复加钱）。 */
    recover_pending_guild_refunds(player);

    /* 每次捐献在扣款前原子分配持久序号并构造唯一 operation id，
     * 即时退款与挂账全链复用同一 id（禁止分支内二次生成——seq=0 复用
     * 会让第二次失败被误判 duplicate 而少退钱）。序号必须先经玩家状态
     * owner 提交成功才允许扣款，避免未落盘的 seq 被重复使用。 */
    seq = player->query("economy/operation_seq") || 0;
    seq++;
    if (!commit_player_state(player, ([ "economy/operation_seq": seq ]),
        ({ }), "guild/operation_seq"))
        return 0;
    operation_id = sprintf("guild_donation_%s_%d", ensure_player_id(player), seq);
    refund_operation_id = sprintf("guild_refund_%s_%d", ensure_player_id(player), seq);

    /* L-95：同一操作身份必须贯穿两域。扣款前先持久“捐赠意图”
     * （state=intent，复用既有挂账表与唯一 recovery owner，不新建账本）：
     * 扣款成功但贡献记账未完成的崩溃窗口里，恢复消费者可依据资金腿
     * receipt 判定该退还是该丢弃，不再出现“钱扣了、业务无记录”。 */
    intent_pending = player->query("economy/guild_refund_pending");
    if (!mapp(intent_pending))
        intent_pending = ([]);
    intent_pending[operation_id] = ([
        "amount": amount,
        "operation_id": operation_id,
        "refund_operation_id": refund_operation_id,
        "created_at": time(),
        "attempts": 0,
        "state": "intent",
    ]);
    if (!commit_player_state(player,
        ([ "economy/guild_refund_pending": intent_pending ]), ({ }),
        "guild/donation_intent"))
        return 0;

    /* 扣款走 economy 域 owner 事务：payload 只携带可序列化的 money
     * mutation（同 operation_id，MONEY_D 账本语义与原 settle 路径一致），
     * handler 委托 MONEY_D 幂等账本真实执行；apply 失败由协调器统一
     * 回滚后上报，guild 流程按“扣款未发生”处理。 */
    money_payload = ([
        "schema_version": 1,
        "money": ([
            "player_id": ensure_player_id(player),
            "operation_id": operation_id,
            "direction": "deduct",
            "amount": amount,
            "money_domain": "guild",
            "money_action": "donation",
        ]),
    ]);
    money_key = "economy/guild_donation/" + operation_id;
    err = catch(created = OWNER_COMMIT_D->create_domain_transaction("economy",
        money_key, ({ "owner/player/" + ensure_player_id(player) }),
        money_payload));
    if (err || !mapp(created) || created["ok"] != 1) {
        log_file("guild", sprintf("%s donation debit txn create failed for %s (op=%s): %O\n",
            ctime(time()), ensure_player_id(player) || "?", operation_id,
            err || created));
        return 0;
    }
    txn_id = mapp(created["transaction"]) ? created["transaction"]["id"] : 0;
    err = catch(prepared = OWNER_COMMIT_D->prepare_transaction(txn_id));
    if (err || !mapp(prepared) || prepared["ok"] != 1) {
        log_file("guild", sprintf("%s donation debit txn prepare failed for %s (op=%s): %O\n",
            ctime(time()), ensure_player_id(player) || "?", operation_id,
            err || prepared));
        return 0;
    }
    err = catch(committed = OWNER_COMMIT_D->commit_transaction(txn_id));
    if (err || !mapp(committed) || committed["ok"] != 1) {
        log_file("guild", sprintf("%s donation debit txn commit failed for %s (op=%s): %O\n",
            ctime(time()), ensure_player_id(player) || "?", operation_id,
            err || committed));
        return 0;
    }

    if (!add_contribution(player, amount, "帮派捐献")) {
        /* 即时退款使用独立 operation id（幂等；同 id 不重复加钱）。 */
        if (!ECONOMY_SETTLEMENT_D->refund_guild_donation(player, amount,
            refund_operation_id)["ok"]) {
            /* 退款失败进可恢复账本（不静默）。键用同一持久序号，
             * 永不复用；entry 记录结构供幂等回放与健康检查。
             * 挂账必须先经玩家状态 owner 提交：提交失败时账本没记录，
             * 退款又失败，只能显式记日志（不可静默丢弃）。 */
            pending = player->query("economy/guild_refund_pending");
            if (!mapp(pending))
                pending = ([]);
            pending[sprintf("donate_%d", seq)] = ([
                "amount": amount,
                "operation_id": refund_operation_id,
                "created_at": time(),
                "attempts": 0,
                "state": "pending",
            ]);
            if (!commit_player_state(player,
                ([ "economy/guild_refund_pending": pending ]), ({ }),
                "guild/refund_pending")) {
                log_file("guild", sprintf(
                    "%s refund pending commit failed for %s (op=%s)\n",
                    ctime(time()), ensure_player_id(player) || "?",
                    refund_operation_id));
                return 0;
            }
            if (!persist_player_data(player))
                log_file("guild", sprintf("%s refund pending save failed for %s\n",
                    ctime(time()), ensure_player_id(player) || "?"));
        }
        mark_donation_intent_completed(player, operation_id);
        return 0;
    }

    mark_donation_intent_completed(player, operation_id);
    return 1;
}

mapping command_receive_guild_salary(object player)
{
    string player_id;
    string guild_id;
    mapping guild;
    mapping member;
    int salary;
    int last_time;
    int seq;
    mapping pending;
    mapping salary_receipt;
    mapping snapshot;

    if (!objectp(player))
        return build_command_result(0, "无效角色。\n", 0);

    player_id = ensure_player_id(player);
    guild_id = _player_guild[player_id];
    guild = ensure_guild(guild_id);
    if (!stringp(player_id) || !mapp(guild))
        return build_command_result(0, "当前没有可领取俸禄的帮派。\n", 0);

    member = ensure_member(guild, player_id);
    if (!mapp(member))
        return build_command_result(0, "当前没有可领取俸禄的帮派身份。\n", 0);

    salary = intp(guild["salary"]) ? guild["salary"] : 0;
    if (salary <= 0)
        salary = DEFAULT_GUILD_SALARY;

    /* 先处理未完成的 claim（发钱后、salary_last 写入前崩溃的
     * 跨日重试复用同 operation id，不双发）。 */
    pending = member["salary_pending"];
    if (mapp(pending) && stringp(pending["operation_id"])) {
        salary_receipt = ECONOMY_SETTLEMENT_D->settle_guild_salary(player,
            pending["amount"], pending["operation_id"]);
        if (mapp(salary_receipt) && salary_receipt["ok"]) {
            snapshot = snapshot_guild_state();
            member["salary_last"] = time();
            if (stringp(pending["operation_id"]))
                MONEY_D->unpin_money_operation(player, pending["operation_id"],
                    "guild/salary_pending");
            map_delete(member, "salary_pending");
            if (!commit_guild_state(snapshot))
                return build_command_result(0,
                    "帮派俸禄状态保存失败，稍后重试。\n", 0);
            /* guild 账本 member 字段是真源；玩家持久 key 只服务迁移读。
             * 镜像提交失败必须可观测，但账本已提交，不回退发款结果。 */
            if (!commit_player_state(player,
                ([ "guild_salary_last": time() ]), ({ }),
                "guild/salary_last"))
                log_file("guild", sprintf(
                    "%s salary_last mirror commit failed for %s\n",
                    ctime(time()), ensure_player_id(player) || "?"));
            player->delete_temp("guild_salary_last");
            return build_command_result(1,
                sprintf("成功领取今日帮派俸禄：%d 文。\n", salary),
                ([ "salary": salary ]));
        }
        snapshot = snapshot_guild_state();
        pending["attempts"] = (pending["attempts"] || 0) + 1;
        pending["state"] = "retry_failed";
        pending["last_error"] = mapp(salary_receipt)
            && salary_receipt["reason"]
            ? salary_receipt["reason"] : "salary failed";
        member["salary_pending"] = pending;
        if (!commit_guild_state(snapshot))
            return build_command_result(0, "帮派俸禄状态保存失败，稍后重试。\n", 0);
        return build_command_result(0, "帮派俸禄发放失败，稍后重试。\n", 0);
    }

    /* 领取守卫：guild 账本 member 字段为真源，player 持久 key 仅迁移读。 */
    last_time = member["salary_last"]
        || player->query("guild_salary_last") || 0;
    if (last_time && time() - last_time < SECONDS_PER_DAY)
        return build_command_result(0, "今日帮派俸禄已经领取过了。\n", 0);

    /* 先分配持久 claim（seq + pending 落盘成功后才发钱）——
     * 跨日崩溃窗口重试复用同 id；save 失败不得发款。 */
    snapshot = snapshot_guild_state();
    seq = member["salary_claim_seq"] || 0;
    seq++;
    member["salary_claim_seq"] = seq;
    member["salary_pending"] = ([
        /* L-94：身份必须区分“同一次成员关系”。旧实现只用 guild+player+seq，
         * 会员离会重入后 seq 从头开始，会撞上旧 receipt 被判 duplicate，
         * 于是俸禄没到账却被标记已领。joined_at 随成员关系生成，重入即新身份。 */
        "operation_id": sprintf("guild_salary_%s_%s_%d_%d", guild_id,
            player_id, member["joined_at"] || 0, seq),
        "amount": salary,
        "created_at": time(),
        "attempts": 0,
        "state": "pending",
    ]);
    if (!commit_guild_state(snapshot))
        return build_command_result(0, "帮派俸禄发放失败，请稍后再试。\n", 0);

    /* L-94：pending 未完成前 pin 住该 receipt——账本裁剪不得丢掉重试
     * 依赖的幂等记录；完成/终结时由 unpin（见下）。 */
    MONEY_D->pin_money_operation(player,
        member["salary_pending"]["operation_id"], "guild/salary_pending");

    salary_receipt = ECONOMY_SETTLEMENT_D->settle_guild_salary(player,
        salary, member["salary_pending"]["operation_id"]);
    if (!mapp(salary_receipt) || !salary_receipt["ok"]) {
        snapshot = snapshot_guild_state();
        member["salary_pending"]["attempts"] = 1;
        member["salary_pending"]["state"] = "retry_failed";
        member["salary_pending"]["last_error"] = mapp(salary_receipt)
            && salary_receipt["reason"]
            ? salary_receipt["reason"] : "salary failed";
        if (!commit_guild_state(snapshot))
            return build_command_result(0, "帮派俸禄状态保存失败，稍后重试。\n", 0);
        return build_command_result(0, "帮派俸禄发放失败，稍后重试。\n", 0);
    }

    snapshot = snapshot_guild_state();
    member["salary_last"] = time();
    map_delete(member, "salary_pending");
    if (!commit_guild_state(snapshot))
        return build_command_result(0,
            "帮派俸禄状态保存失败，稍后重试。\n", 0);
    /* guild 账本 member 字段是真源；玩家持久 key 只服务迁移读。
     * 镜像提交失败必须可观测，但账本已提交，不回退发款结果。 */
    if (!commit_player_state(player, ([ "guild_salary_last": time() ]),
        ({ }), "guild/salary_last"))
        log_file("guild", sprintf(
            "%s salary_last mirror commit failed for %s\n",
            ctime(time()), ensure_player_id(player) || "?"));
    player->delete_temp("guild_salary_last");
    return build_command_result(1,
        sprintf("成功领取今日帮派俸禄：%d 文。\n", salary),
        ([ "salary": salary ]));
}

int receive_guild_salary(object player)
{
    mapping result;

    result = command_receive_guild_salary(player);
    return mapp(result) && result["success"];
}
