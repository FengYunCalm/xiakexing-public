/**
 * @file    team_d.c
 * @brief   组队服务真源



 */

#pragma strict_types
#pragma save_types

#include <system/constants.h>
#include <system/ansi.h>
#include <system/services.h>

inherit "/lib/framework/daemon";

// Team membership and leader assignment are canonical here. Legacy social
// wrappers may forward into this daemon, but should not retain parallel team state.
// 当前状态全部为 nosave runtime ledger：重启后队伍不恢复，这是设计事实，不是偶发遗漏。
nosave mapping _teams;
nosave mapping _player_team;
nosave mapping _pending_invites;
nosave int _team_seq;
nosave int _owner_operation_seq;

private object find_online_member(string pid);
private void clear_pending_invite_by_invitee_id(string invitee_id);
private int has_active_dungeon_session(object player);
int is_team_leader(object player);

private string ensure_player_id(object player)
{
    string pid;

    if (!objectp(player))
        return 0;
    pid = get_object_id(player);
    if (!stringp(pid) || pid == "")
        return 0;
    return pid;
}

private mapping ensure_team(string team_id)
{
    if (!stringp(team_id) || !mapp(_teams[team_id]))
        return 0;
    return _teams[team_id];
}

private mapping ensure_pending_invite(string invitee_id)
{
    mapping invite;

    if (!stringp(invitee_id) || !mapp(_pending_invites))
        return 0;

    invite = _pending_invites[invitee_id];
    if (!mapp(invite)
        || !stringp(invite["leader_id"]) || invite["leader_id"] == ""
        || invite["leader_id"] == invitee_id
        || !intp(invite["issued_at"])) {
        map_delete(_pending_invites, invitee_id);
        return 0;
    }

    return invite;
}

private mapping resolve_pending_invite(string invitee_id)
{
    mapping invite;
    object invitee;
    object leader;

    invite = ensure_pending_invite(invitee_id);
    if (!mapp(invite))
        return 0;

    invitee = find_online_member(invitee_id);
    leader = find_online_member(invite["leader_id"]);
    if (!objectp(invitee) || !objectp(leader)) {
        clear_pending_invite_by_invitee_id(invitee_id);
        return 0;
    }

    if (stringp(_player_team[invitee_id]) || has_active_dungeon_session(invitee)
        || has_active_dungeon_session(leader)) {
        clear_pending_invite_by_invitee_id(invitee_id);
        return 0;
    }

    if (stringp(_player_team[invite["leader_id"]]) && !is_team_leader(leader)) {
        clear_pending_invite_by_invitee_id(invitee_id);
        return 0;
    }

    return invite;
}

private object find_online_member(string pid)
{
    if (!stringp(pid) || pid == "")
        return 0;

    return find_player_online(pid);
}

private object *online_members(mapping team)
{
    object *out;
    string pid;
    object ob;

    out = ({ });
    if (!mapp(team))
        return out;

    foreach (pid in team["members"]) {
        ob = find_online_member(pid);
        if (objectp(ob))
            out += ({ ob });
    }
    return out;
}

private void clear_pending_invite_by_invitee_id(string invitee_id)
{
    if (!mapp(_pending_invites) || !stringp(invitee_id) || invitee_id == "")
        return;

    map_delete(_pending_invites, invitee_id);
}

private void clear_pending_invites_for_leader_id(string leader_id)
{
    string invitee_id;

    if (!mapp(_pending_invites) || !stringp(leader_id) || leader_id == "")
        return;

    foreach (invitee_id in keys(_pending_invites)) {
        if (mapp(_pending_invites[invitee_id])
            && _pending_invites[invitee_id]["leader_id"] == leader_id)
            map_delete(_pending_invites, invitee_id);
    }
}

private int query_pending_invite_count_for_leader_id(string leader_id)
{
    int count;
    string invitee_id;

    count = 0;
    if (!mapp(_pending_invites) || !stringp(leader_id) || leader_id == "")
        return 0;

    foreach (invitee_id in keys(_pending_invites)) {
        if (mapp(resolve_pending_invite(invitee_id))
            && _pending_invites[invitee_id]["leader_id"] == leader_id)
            count += 1;
    }

    return count;
}

private int has_active_dungeon_session(object player)
{
    return objectp(player) && mapp(DUNGEON_D->get_player_dungeon(player));
}

private mapping build_team_result(int ok, string error, string message, mapping extra)
{
    mapping result;
    string key;

    result = ([
        "ok": ok,
        "error": stringp(error) ? error : "",
        "message": stringp(message) ? message : "",
    ]);
    if (!mapp(extra))
        return result;

    foreach (key in keys(extra))
        result[key] = extra[key];
    return result;
}

private void notify_dungeon_team_change(mapping event)
{
    if (!mapp(event))
        return;

    DUNGEON_D->on_team_membership_changed(event);
}

void create()
{
    ::create();
    if (!mapp(_teams))
        _teams = ([]);
    if (!mapp(_player_team))
        _player_team = ([]);
    if (!mapp(_pending_invites))
        _pending_invites = ([]);
    _owner_operation_seq = 0;
}

private int owner_team_internal(object player)
{
    return objectp(player) && player->query_temp("owner/internal/team") == 1;
}

private mapping commit_owner_team_operation(object actor, string operation,
    string target_player_id)
{
    mapping created;
    mapping prepared;
    mapping committed;
    mapping transaction;
    mapping applied;
    mapping payload;
    string actor_id;
    string idempotency_key;
    string *owners;

    actor_id = ensure_player_id(actor);
    if (!stringp(actor_id) || actor_id == "" || !stringp(operation)
        || operation == "")
        return ([ "ok": 0, "error": "owner team proposal is invalid" ]);
    _owner_operation_seq++;
    idempotency_key = sprintf("team/%s/%s/%d/%d", operation, actor_id,
        time(), _owner_operation_seq);
    payload = ([
        "schema_version": 1,
        "team": ([
            "operation": operation,
            "actor_player_id": actor_id,
            "target_player_id": stringp(target_player_id)
                ? target_player_id : "",
        ]),
    ]);
    owners = ({ "owner/player/" + actor_id,
        "owner/global/service/team" });
    created = OWNER_COMMIT_D->create_domain_transaction("team",
        idempotency_key, owners, payload);
    if (!mapp(created) || created["ok"] != 1
        || !mapp(created["transaction"]))
        return ([ "ok": 0, "error": "team owner transaction create failed",
            "owner_transaction": created ]);
    prepared = OWNER_COMMIT_D->prepare_transaction(
        created["transaction"]["id"]);
    if (!mapp(prepared) || prepared["ok"] != 1)
        return ([ "ok": 0, "error": "team owner transaction prepare failed",
            "owner_transaction": prepared ]);
    committed = OWNER_COMMIT_D->commit_transaction(
        created["transaction"]["id"]);
    if (!mapp(committed) || committed["ok"] != 1)
        return ([ "ok": 0, "error": "team owner transaction commit failed",
            "owner_transaction": committed ]);
    transaction = committed["transaction"];
    applied = mapp(transaction) ? transaction["apply_result"] : 0;
    if (!mapp(applied) || applied["ok"] != 1)
        return ([ "ok": 0, "error": "team owner apply receipt missing",
            "owner_transaction": committed ]);
    return ([ "ok": 1, "data": applied,
        "owner_transaction": committed ]);
}

mapping query_pending_invite_snapshot(object player)
{
    mapping invite;
    string player_id;

    player_id = ensure_player_id(player);
    invite = resolve_pending_invite(player_id);
    if (!mapp(invite))
        return 0;

    return ([
        "leader_id": invite["leader_id"],
        "invitee_id": player_id,
        "issued_at": invite["issued_at"],
    ]);
}

string query_team_id(object player)
{
    return _player_team[ensure_player_id(player)];
}

object query_leader(object player)
{
    mapping team;
    string leader_id;

    team = ensure_team(query_team_id(player));
    if (!mapp(team))
        return 0;

    leader_id = team["leader"];
    return stringp(leader_id) ? find_online_member(leader_id) : 0;
}

object *query_team(object player)
{
    return online_members(ensure_team(query_team_id(player)));
}

string query_team_leader_id(object player)
{
    mapping team;

    team = ensure_team(query_team_id(player));
    return mapp(team) ? (team["leader"] || "") : "";
}

string *query_team_member_ids(object player)
{
    mapping team;

    team = ensure_team(query_team_id(player));
    if (!mapp(team) || !arrayp(team["members"]))
        return ({ });

    return team["members"] + ({ });
}

/* Stable-ID snapshot used by owner transactions. The snapshot intentionally
 * contains no player objects; notification is emitted only by the successful
 * forward operation, while a failed proposal restores the owner ledger. */
mapping query_owner_proposal_snapshot()
{
    mixed snapshot;
    mixed err;

    err = catch(snapshot = restore_variable(save_variable(([
        "teams": _teams || ([]),
        "player_team": _player_team || ([]),
        "pending_invites": _pending_invites || ([]),
        "team_seq": _team_seq,
    ]))));
    return !err && mapp(snapshot) ? snapshot : 0;
}

int restore_owner_proposal_snapshot(mapping snapshot)
{
    mixed restored;
    mixed err;

    if (!mapp(snapshot) || !mapp(snapshot["teams"])
        || !mapp(snapshot["player_team"])
        || !mapp(snapshot["pending_invites"])
        || !intp(snapshot["team_seq"]) || snapshot["team_seq"] < 0)
        return 0;
    err = catch(restored = restore_variable(save_variable(snapshot)));
    if (err || !mapp(restored))
        return 0;
    _teams = restored["teams"];
    _player_team = restored["player_team"];
    _pending_invites = restored["pending_invites"];
    _team_seq = restored["team_seq"];
    return 1;
}

object query_team_member_object(string member_id)
{
    return find_online_member(member_id);
}

int is_team_leader(object player)
{
    mapping team;
    string pid;

    pid = ensure_player_id(player);
    team = ensure_team(_player_team[pid]);
    return stringp(pid) && mapp(team) && team["leader"] == pid;
}

int set_team_member(object leader, object target)
{
    mapping team;
    string leader_id;
    string target_id;
    string team_id;

    leader_id = ensure_player_id(leader);
    target_id = ensure_player_id(target);
    if (!stringp(leader_id) || !stringp(target_id) || leader == target)
        return 0;

    if (stringp(_player_team[target_id]))
        return 0;

    team_id = _player_team[leader_id];
    team = ensure_team(team_id);
    if (mapp(team) && team["leader"] != leader_id)
        return 0;
    if (!mapp(team)) {
        _team_seq += 1;
        team_id = sprintf("team_%d_%d", time(), _team_seq);
        team = ([
            "id": team_id,
            "leader": leader_id,
            "members": ({ leader_id })
        ]);
        _teams[team_id] = team;
        _player_team[leader_id] = team_id;
    }

    if (sizeof(team["members"]) >= MAX_TEAM_SIZE)
        return 0;

    if (member_array(target_id, team["members"]) == -1)
        team["members"] += ({ target_id });
    _player_team[target_id] = team_id;
    clear_pending_invite_by_invitee_id(leader_id);
    clear_pending_invite_by_invitee_id(target_id);
    clear_pending_invites_for_leader_id(target_id);
    if (sizeof(team["members"]) >= MAX_TEAM_SIZE)
        clear_pending_invites_for_leader_id(team["leader"]);
    return 1;
}

private mapping request_team_invite_internal(object leader, object target)
{
    mapping team;
    mapping invite;
    string leader_id;
    string target_id;
    int team_size;
    int pending_count;

    leader_id = ensure_player_id(leader);
    target_id = ensure_player_id(target);
    if (!stringp(leader_id) || !stringp(target_id) || leader == target)
        return build_team_result(0, "invalid_target", "组队邀请失败。\n", 0);

    if (stringp(_player_team[target_id]))
        return build_team_result(0, "target_in_team", "对方已经在队伍中了。\n", 0);

    if (mapp(query_pending_invite_snapshot(leader)))
        return build_team_result(0, "leader_has_pending_invite", "你还有待处理的组队邀请，先决定是否加入别人的队伍。\n", 0);

    if (has_active_dungeon_session(leader) || has_active_dungeon_session(target))
        return build_team_result(0, "dungeon_locked", "副本进行中不能邀请或加入队伍。\n", 0);

    team = ensure_team(_player_team[leader_id]);
    if (mapp(team) && team["leader"] != leader_id)
        return build_team_result(0, "not_team_leader", "只有队长才能继续邀请队员。\n", 0);

    invite = query_pending_invite_snapshot(target);
    if (mapp(invite) && invite["leader_id"] == leader_id)
        return build_team_result(0, "duplicate_invite", "对方已经收到了你的组队邀请。\n", 0);
    if (mapp(invite))
        return build_team_result(0, "target_has_pending_invite", "对方还有待处理的组队邀请。\n", 0);

    team_size = mapp(team) ? sizeof(team["members"] || ({ })) : 1;
    pending_count = query_pending_invite_count_for_leader_id(leader_id);
    if (team_size + pending_count >= MAX_TEAM_SIZE)
        return build_team_result(0, "team_full", "你的队伍人数已满，不能再邀请更多人。\n", 0);

    _pending_invites[target_id] = ([
        "leader_id": leader_id,
        "issued_at": time(),
    ]);
    return build_team_result(1, "", "组队邀请已发出。\n", ([
        "leader_id": leader_id,
        "target_id": target_id,
    ]));
}

private mapping accept_team_invite_internal(object player)
{
    mapping invite;
    object leader;
    string player_id;

    player_id = ensure_player_id(player);
    invite = query_pending_invite_snapshot(player);
    if (!mapp(invite))
        return build_team_result(0, "no_pending_invite", "你当前没有待处理的组队邀请。\n", 0);

    leader = find_online_member(invite["leader_id"]);
    if (!objectp(leader)) {
        clear_pending_invite_by_invitee_id(player_id);
        return build_team_result(0, "invite_expired", "这份组队邀请已经失效。\n", 0);
    }

    if (!set_team_member(leader, player)) {
        clear_pending_invite_by_invitee_id(player_id);
        return build_team_result(0, "invite_invalid", "这份组队邀请已经失效。\n", 0);
    }

    return build_team_result(1, "", "你接受了组队邀请。\n", ([
        "leader_id": invite["leader_id"],
        "team_id": query_team_id(player),
    ]));
}

private mapping reject_team_invite_internal(object player)
{
    mapping invite;
    string player_id;

    player_id = ensure_player_id(player);
    invite = query_pending_invite_snapshot(player);
    if (!mapp(invite))
        return build_team_result(0, "no_pending_invite", "你当前没有待处理的组队邀请。\n", 0);

    clear_pending_invite_by_invitee_id(player_id);
    return build_team_result(1, "", "你拒绝了这份组队邀请。\n", ([
        "leader_id": invite["leader_id"],
    ]));
}

private int dismiss_team_internal(object leader)
{
    mapping team;
    string team_id;
    string pid;
    string *member_ids;
    string actor_id;

    if (!is_team_leader(leader))
        return 0;

    team_id = query_team_id(leader);
    team = ensure_team(team_id);
    if (!mapp(team))
        return 0;

    member_ids = team["members"] + ({ });
    actor_id = ensure_player_id(leader);
    clear_pending_invites_for_leader_id(actor_id);

    foreach (pid in team["members"])
        map_delete(_player_team, pid);
    map_delete(_teams, team_id);
    notify_dungeon_team_change(([
        "event": "dismiss",
        "team_id": team_id,
        "actor_id": actor_id,
        "affected_ids": member_ids,
        "member_ids_before": member_ids,
        "member_ids_after": ({ }),
    ]));
    return 1;
}

private int leave_team_internal(object player)
{
    mapping team;
    string pid;
    string team_id;
    string *before_ids;
    string *after_ids;
    int idx;

    pid = ensure_player_id(player);
    team_id = _player_team[pid];
    team = ensure_team(team_id);
    if (!stringp(pid) || !mapp(team))
        return 0;

    if (team["leader"] == pid)
        return dismiss_team_internal(player);

    before_ids = team["members"] + ({ });

    idx = member_array(pid, team["members"]);
    if (idx != -1)
        team["members"] = team["members"][0..idx-1] + team["members"][idx+1..];
    map_delete(_player_team, pid);
    after_ids = team["members"] + ({ });
    notify_dungeon_team_change(([
        "event": "leave",
        "team_id": team_id,
        "actor_id": pid,
        "affected_ids": ({ pid }),
        "member_ids_before": before_ids,
        "member_ids_after": after_ids,
    ]));
    return 1;
}

private int kick_team_member_internal(object leader, object member)
{
    mapping team;
    string member_id;
    string leader_id;
    string team_id;
    string *before_ids;
    string *after_ids;
    int idx;

    if (!is_team_leader(leader))
        return 0;

    member_id = ensure_player_id(member);
    leader_id = ensure_player_id(leader);
    team_id = query_team_id(leader);
    team = ensure_team(team_id);
    if (!stringp(member_id) || !mapp(team) || team["leader"] == member_id)
        return 0;

    idx = member_array(member_id, team["members"]);
    if (idx == -1)
        return 0;

    before_ids = team["members"] + ({ });
    team["members"] = team["members"][0..idx-1] + team["members"][idx+1..];
    map_delete(_player_team, member_id);
    after_ids = team["members"] + ({ });
    notify_dungeon_team_change(([
        "event": "kick",
        "team_id": team_id,
        "actor_id": leader_id,
        "affected_ids": ({ member_id }),
        "member_ids_before": before_ids,
        "member_ids_after": after_ids,
    ]));
    return 1;
}

mapping request_team_invite(object leader, object target)
{
    mapping transaction;
    string target_id;

    if (owner_team_internal(leader))
        return request_team_invite_internal(leader, target);
    target_id = ensure_player_id(target);
    transaction = commit_owner_team_operation(leader, "invite", target_id);
    if (!mapp(transaction) || transaction["ok"] != 1)
        return build_team_result(0, "owner_transaction_failed",
            "组队邀请提交失败，请稍后重试。\n", transaction);
    return build_team_result(1, "", "组队邀请已发出。\n",
        transaction["data"]);
}

mapping accept_team_invite(object player)
{
    mapping transaction;

    if (owner_team_internal(player))
        return accept_team_invite_internal(player);
    transaction = commit_owner_team_operation(player, "accept", "");
    if (!mapp(transaction) || transaction["ok"] != 1)
        return build_team_result(0, "owner_transaction_failed",
            "接受组队邀请提交失败，请稍后重试。\n", transaction);
    return build_team_result(1, "", "你接受了组队邀请。\n",
        transaction["data"]);
}

mapping reject_team_invite(object player)
{
    mapping transaction;

    if (owner_team_internal(player))
        return reject_team_invite_internal(player);
    transaction = commit_owner_team_operation(player, "reject", "");
    if (!mapp(transaction) || transaction["ok"] != 1)
        return build_team_result(0, "owner_transaction_failed",
            "拒绝组队邀请提交失败，请稍后重试。\n", transaction);
    return build_team_result(1, "", "你拒绝了这份组队邀请。\n",
        transaction["data"]);
}

int dismiss_team(object leader)
{
    mapping transaction;

    if (owner_team_internal(leader))
        return dismiss_team_internal(leader);
    transaction = commit_owner_team_operation(leader, "dismiss", "");
    return mapp(transaction) && transaction["ok"] == 1;
}

int leave_team(object player)
{
    mapping transaction;

    if (owner_team_internal(player))
        return leave_team_internal(player);
    transaction = commit_owner_team_operation(player, "leave", "");
    return mapp(transaction) && transaction["ok"] == 1;
}

int kick_team_member(object leader, object member)
{
    mapping transaction;
    string member_id;

    if (owner_team_internal(leader))
        return kick_team_member_internal(leader, member);
    member_id = ensure_player_id(member);
    transaction = commit_owner_team_operation(leader, "kick", member_id);
    return mapp(transaction) && transaction["ok"] == 1;
}

int kick_team_member_id(object leader, string member_id)
{
    mapping team;
    string *before_ids;
    string *after_ids;
    string leader_id;
    int idx;

    if (!is_team_leader(leader))
        return 0;
    if (!stringp(member_id) || member_id == "")
        return 0;

    team = ensure_team(query_team_id(leader));
    if (!mapp(team) || team["leader"] == member_id)
        return 0;

    before_ids = team["members"] + ({ });
    leader_id = ensure_player_id(leader);

    idx = member_array(member_id, team["members"]);
    if (idx == -1)
        return 0;

    team["members"] = team["members"][0..idx-1] + team["members"][idx+1..];
    map_delete(_player_team, member_id);
    after_ids = team["members"] + ({ });
    notify_dungeon_team_change(([
        "event": "kick",
        "team_id": query_team_id(leader),
        "actor_id": leader_id,
        "affected_ids": ({ member_id }),
        "member_ids_before": before_ids,
        "member_ids_after": after_ids,
    ]));
    return 1;
}
