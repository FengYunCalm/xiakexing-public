/**
 * @file    rank_d.c
 * @brief   排行服务真源



 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <system/constants.h>

inherit "/lib/framework/daemon";

// Ranking order, player rank cache, and reward schedules are owned here.
// `rank_core.c` / `rank_query.c` / `rank_reward.c` are compatibility facades
// for legacy call sites and should not evolve into competing ranking sources.

nosave mapping _rankings;
nosave mapping _player_ranks;
nosave mapping _rank_rewards;
nosave mapping _rank_view_cache;

int query_player_rank(string player_id, string rank_type);
mixed *query_ranking(string rank_type, int count);
int query_total_entries(string rank_type);

mapping query_rank_owner_route(string key)
{
    mapping route;
    string route_key;
    mixed err;

    route_key = stringp(key) && key != "" ? key : "rank";
    err = catch(route = OWNER_RUNTIME_D->query_service_owner_route("rank", route_key));
    if (err || !mapp(route) || route["ok"] != 1)
        return ([
            "ok": 0,
            "service": "rank",
            "key": route_key,
            "failure_reason": err ? sprintf("owner route query failed: %O", err)
                : (mapp(route) ? route["failure_reason"] : "owner route unavailable"),
            "readiness": mapp(route) ? route["readiness"] : ([ "ok": 0 ]),
            "dependencies": mapp(route) ? route["dependencies"] : ([ ]),
            "direct_cross_owner_write": 0,
        ]);

    route["commit_boundary"] = "proposal_commit";
    route["direct_cross_owner_write"] = 0;
    return route;
}

mapping query_rank_owner_status()
{
    mapping route;

    route = query_rank_owner_route("rank");
    if (!mapp(route) || route["ok"] != 1)
        return route;
    return ([
        "service_map_v2": 1,
        "service": "rank",
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

private string ensure_player_id(object player)
{
    string player_id;

    if (!objectp(player))
        return "";
    if (function_exists("query_account_id", player)) {
        player_id = player->query_account_id() || "";
        if (player_id != "")
            return player_id;
    }
    return get_object_id(player);
}

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

mapping query_rankings() { return copy(_rankings || ([])); }
mapping query_player_ranks() { return copy(_player_ranks || ([])); }
mapping query_rank_rewards() { return copy(_rank_rewards || ([])); }

private string make_rank_view_key(string rank_type, int count)
{
    return sprintf("rank/view/%s/%d", rank_type || "", count);
}

private mapping build_rank_view_snapshot(string rank_type, int count)
{
    mixed entries;
    int total;

    entries = _rankings[rank_type];
    total = arrayp(entries) ? sizeof(entries) : 0;
    if (!arrayp(entries))
        entries = ({ });
    if (count <= 0 || count > sizeof(entries))
        count = sizeof(entries);
    entries = count > 0 ? copy(entries[0..count-1]) : ({ });
    return ([
        "type": "rank_view",
        "rank_type": rank_type,
        "count": count,
        "entries": entries,
        "total": total,
        "generated_at": time(),
    ]);
}

private void commit_rank_view_cache(string key, mapping snapshot, mapping digest)
{
    int input_hash;
    mapping proposal;
    mapping submitted;
    mapping committed;

    if (!mapp(snapshot) || !mapp(digest) || !digest["ok"])
        return;
    input_hash = intp(digest["async_input_hash"]) ? digest["async_input_hash"] : 0;
    if (input_hash == 0)
        return;
    if (!mapp(_rank_view_cache))
        _rank_view_cache = ([]);

    proposal = ([
        "type": "rank_view_cache",
        "owner_key": key,
        "input_hash": input_hash,
        "payload": snapshot,
        "expires_at": time() + 30,
    ]);
    submitted = PROPOSAL_COMMIT_D->submit_proposal(proposal);
    if (mapp(submitted) && submitted["ok"]) {
        committed = PROPOSAL_COMMIT_D->commit_proposal(submitted["proposal_id"], ([
            "owner_key": key,
            "input_hash": input_hash,
        ]), ([ "cache_key": key ]));
        if (mapp(committed) && committed["ok"]) {
            _rank_view_cache[key] = copy(snapshot);
            _rank_view_cache[key]["digest"] = copy(digest);
            _rank_view_cache[key]["committed_at"] = time();
        }
    }
}

mapping query_async_rank_view(string rank_type, int count)
{
    string key;
    mapping snapshot;
    mapping digest_response;
    mapping digest;
    mapping cached;

    if (!stringp(rank_type) || rank_type == "")
        rank_type = "power";
    if (count <= 0)
        count = 10;
    if (count > 100)
        count = 100;

    key = make_rank_view_key(rank_type, count);
    snapshot = build_rank_view_snapshot(rank_type, count);
    if (!mapp(_rank_view_cache))
        _rank_view_cache = ([]);
    digest_response = ASYNC_COMPUTE_D->query_or_submit(key, "snapshot_digest", snapshot, ([
        "owner_key": key,
        "repeat": 32,
        "timeout_ms": 3000,
        "ttl_ms": 30000,
    ]));
    digest = mapp(digest_response) && mapp(digest_response["result_spec"])
        ? copy(digest_response["result_spec"])
        : ([
            "ok": 0,
            "state": mapp(digest_response) ? (digest_response["async_state"] || digest_response["status"] || "pending") : "pending",
        ]);
    if (mapp(digest) && mapp(digest_response) && digest_response["success"]) {
        digest["ok"] = 1;
        digest["state"] = digest_response["async_state"] || "ready";
        commit_rank_view_cache(key, snapshot, digest);
    }

    cached = _rank_view_cache[key];
    if (!mapp(cached))
        cached = snapshot;
    cached = copy(cached);
    cached["async"] = 1;
    cached["async_key"] = key;
    cached["async_state"] = digest["state"] || "pending";
    cached["digest"] = digest;
    cached["source"] = "rank_d.async_rank_view";
    cached["commit_boundary"] = "proposal_commit";
    return cached;
}

mapping query_rank_runtime_snapshot()
{
    string rank_type;
    mapping rankings;
    mapping rewards;
    mapping counts;

    rankings = _rankings || ([]);
    rewards = _rank_rewards || ([]);
    counts = ([]);

    foreach (rank_type in keys(rankings))
        counts[rank_type] = arrayp(rankings[rank_type]) ? sizeof(rankings[rank_type]) : 0;

    return ([
        "ranking_types": sort_array(keys(rankings), 1),
        "reward_keys": sort_array(keys(rewards), 1),
        "entry_counts": counts,
        "player_rank_cache": copy(_player_ranks || ([])),
    ]);
}

int query_player_claim_last(object player, string rank_type)
{
    if (!objectp(player) || !stringp(rank_type) || rank_type == "")
        return 0;
    return player->query("rank_claim_last/" + rank_type) || 0;
}

int clear_player_claim_last(object player, string rank_type)
{
    mapping response;
    string path;

    if (!objectp(player) || !stringp(rank_type) || rank_type == "")
        return 0;
    /* 领取标记是玩家持久状态：删除经玩家状态 owner 提交，
     * 失败返回 0，调用方不得当作已清除。 */
    path = "rank_claim_last/" + rank_type;
    response = PLAYER_STATE_D->commit_state_deletions(player, ({ path }),
        PLAYER_STATE_D->build_state_idempotency_key(player,
            "rank/claim_marker_clear", path));
    return mapp(response) && response["ok"];
}

mapping query_player_rank_snapshot(object player)
{
    mapping snapshot;
    string player_id;
    string rank_type;
    int rank;

    snapshot = ([]);
    player_id = ensure_player_id(player);
    if (player_id == "")
        return snapshot;

    foreach (rank_type in keys(_rankings || ([]))) {
        rank = query_player_rank(player_id, rank_type);
        if (rank > 0)
            snapshot[rank_type] = rank;
    }

    return snapshot;
}

private void refresh_rank_cache(string rank_type, mixed *entries)
{
    mapping updated;
    mapping entry;
    string player_id;
    int index;

    updated = ([]);
    if (!arrayp(entries))
        entries = ({ });

    for (index = 0; index < sizeof(entries); index++) {
        entry = entries[index];
        if (!mapp(entry))
            continue;
        player_id = entry["player_id"];
        if (!stringp(player_id))
            continue;
        if (!mapp(_player_ranks[player_id]))
            _player_ranks[player_id] = ([]);
        _player_ranks[player_id][rank_type] = index + 1;
        updated[player_id] = 1;
    }

    foreach (player_id in keys(_player_ranks)) {
        if (!updated[player_id])
            map_delete(_player_ranks[player_id], rank_type);
    }
}

void create()
{
    ::create();
    if (!mapp(_rankings))
        _rankings = ([]);
    if (!mapp(_player_ranks))
        _player_ranks = ([]);
    if (!mapp(_rank_rewards))
        _rank_rewards = ([]);
    if (!mapp(_rank_view_cache))
        _rank_view_cache = ([]);
}

int add_rank_entry(string rank_type, string player_id, int score)
{
    mixed *entries;
    mapping entry;
    int i;
    int size;

    if (!stringp(rank_type) || !stringp(player_id))
        return 0;

    entries = _rankings[rank_type];
    if (!arrayp(entries))
        entries = ({ });

    entry = (["player_id": player_id, "score": score, "time": time()]);
    size = sizeof(entries);
    for (i = 0; i < size; i++) {
        if (entries[i]["player_id"] == player_id) {
            entries[i] = entry;
            break;
        }
    }
    if (i >= size)
        entries += ({ entry });

    entries = sort_array(entries, (: $2["score"] - $1["score"] :));
    _rankings[rank_type] = entries;
    refresh_rank_cache(rank_type, entries);
    return 1;
}

int remove_rank_entry(string rank_type, string player_id)
{
    mixed *entries;
    mixed *filtered;
    mapping entry;

    entries = _rankings[rank_type];
    if (!arrayp(entries))
        return 0;
    filtered = ({ });
    foreach (entry in entries) {
        if (entry["player_id"] != player_id)
            filtered += ({ entry });
    }
    _rankings[rank_type] = filtered;
    refresh_rank_cache(rank_type, filtered);
    return 1;
}

mixed *query_ranking(string rank_type, int count)
{
    mixed *entries;

    entries = _rankings[rank_type];
    if (!arrayp(entries))
        return ({ });
    if (count <= 0 || count > sizeof(entries))
        count = sizeof(entries);
    return copy(entries[0..count-1]);
}

int query_player_rank(string player_id, string rank_type)
{
    mapping ranks;

    ranks = _player_ranks[player_id];
    if (!mapp(ranks) || !intp(ranks[rank_type]))
        return 0;
    return ranks[rank_type];
}

mixed query_rank_entry(string rank_type, int rank)
{
    mixed *entries;

    entries = _rankings[rank_type];
    if (!arrayp(entries) || rank < 1 || rank > sizeof(entries))
        return 0;
    return copy(entries[rank - 1]);
}

int query_total_entries(string rank_type)
{
    mixed *entries;

    entries = _rankings[rank_type];
    return arrayp(entries) ? sizeof(entries) : 0;
}

void register_rank_reward(string rank_type, int rank_level, mapping reward)
{
    string key;

    key = rank_type + "#" + rank_level;
    _rank_rewards[key] = reward;
}

mixed query_rank_reward(string rank_type, int rank_level)
{
    return _rank_rewards[rank_type + "#" + rank_level];
}

private mapping settle_rank_claim(object player, string rank_type, int rank,
    mapping reward, int claim_time)
{
    mapping settlement;
    mapping marker;
    string settlement_key;
    string marker_path;
    int claim_day;

    if (!objectp(player) || !stringp(rank_type) || rank_type == ""
        || !mapp(reward) || claim_time <= 0)
        return ([ "ok": 0, "error": "invalid_rank_claim" ]);

    claim_day = claim_time / SECONDS_PER_DAY;
    settlement_key = "rank:" + hash("sha256", sprintf("%s:%d:%d",
        rank_type, rank, claim_day));
    settlement = REWARD_SETTLEMENT_D->settle_reward(player, settlement_key,
        reward, ([
            "source_content_id": rank_type,
            "resolution_token": sprintf("%d:%d", rank, claim_day),
        ]));
    if (!mapp(settlement) || !settlement["ok"])
        return settlement;

    marker_path = "rank_claim_last/" + rank_type;
    marker = REWARD_SETTLEMENT_D->commit_claim_marker(player,
        settlement_key, marker_path, claim_time);
    if (!mapp(marker) || !marker["ok"])
        return marker;
    return settlement + ([ "marker": marker ]);
}

int grant_rank_reward(object player, string rank_type, int rank)
{
    mapping reward;
    mapping result;
    int claim_time;

    if (!objectp(player))
        return 0;

    reward = query_rank_reward(rank_type, rank);
    if (!mapp(reward))
        return 0;
    claim_time = time();
    result = settle_rank_claim(player, rank_type, rank, reward, claim_time);
    return mapp(result) && result["ok"];
}

varargs mapping command_claim_rank_reward(object player, string rank_type,
    mapping response_context)
{
    string player_id;
    mapping reward;
    mapping result;
    int rank;
    int claim_time;
    int last_time;

    if (!objectp(player) || !stringp(rank_type) || rank_type == "")
        return build_command_result(0, "无效的排行类型。\n", 0);

    player_id = ensure_player_id(player);
    if (player_id == "")
        return build_command_result(0, "无法确认当前玩家身份。\n", 0);

    rank = query_player_rank(player_id, rank_type);
    if (rank <= 0)
        return build_command_result(0, "当前未进入该榜单，无法领取排行奖励。\n", 0);

    reward = query_rank_reward(rank_type, rank);
    if (!mapp(reward))
        return build_command_result(0, "当前名次没有可领取的排行奖励。\n", 0);

    last_time = player->query("rank_claim_last/" + rank_type) || 0;
    if (last_time && time() - last_time < SECONDS_PER_DAY)
        return build_command_result(0, "今日该榜排行奖励已经领取过了。\n", 0);

    claim_time = time();
    result = settle_rank_claim(player, rank_type, rank, reward, claim_time);
    if (!mapp(result) || !result["ok"])
        return build_command_result(0, "排行奖励领取失败。", ([ "settlement": result ]));

    REWARD_FEEDBACK_D->sync_reward_client_state(player,
        result["applied"] || ([]), response_context);
    return build_command_result(1, REWARD_FEEDBACK_D->format_reward_notice("领取排行奖励成功：", reward), ([
        "rank": rank,
        "reward": reward,
        "applied": result["applied"] || ([]),
    ]));
}

void distribute_daily_rewards()
{
    string rank_type;
    mixed *entries;
    mapping entry;
    object player;
    int rank;

    foreach (rank_type, entries in _rankings)
    {
        rank = 1;
        foreach (entry in entries)
        {
            player = find_player_online(entry["player_id"]);
            if (objectp(player))
                grant_rank_reward(player, rank_type, rank);
            rank++;
            if (rank > 10)
                break;
        }
    }
}
