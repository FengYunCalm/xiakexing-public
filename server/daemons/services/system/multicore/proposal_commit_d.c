/**
 * @file    proposal_commit_d.c
 * @brief   多核 proposal 主线程提交守门服务
 *
 * worker 只能产生 proposal；本服务记录并校验 proposal 的 owner、epoch、
 * 输入 hash 和过期时间，业务服务通过校验后才能在主线程提交真实状态。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

#define MAX_PENDING_PROPOSALS 8192
#define MAX_PROPOSAL_TRACE_HISTORY 8192

nosave mapping _proposals;
nosave mapping _commit_trace;
nosave mapping _commit_trace_order;
nosave mapping _commit_trace_positions;
nosave int _commit_trace_order_head;
nosave int _commit_trace_order_tail;
nosave int _submitted_count;
nosave int _committed_count;
nosave int _rejected_count;
nosave int _pending_capacity_rejections;
nosave int _trace_evictions;

void create()
{
    ::create();
    _proposals = ([]);
    _commit_trace = ([]);
    _commit_trace_order = ([]);
    _commit_trace_positions = ([]);
    _commit_trace_order_head = 1;
    _commit_trace_order_tail = 1;
    _submitted_count = 0;
    _committed_count = 0;
    _rejected_count = 0;
    _pending_capacity_rejections = 0;
    _trace_evictions = 0;
}

private string make_proposal_id(mapping proposal)
{
    return sprintf("%s/%d/%d", proposal["owner_key"] || "global", time(), _submitted_count + 1);
}

private void prune_proposal_trace_history()
{
    string proposal_id;
    int position;

    while (_commit_trace_order_tail - _commit_trace_order_head >
        MAX_PROPOSAL_TRACE_HISTORY) {
        position = _commit_trace_order_head;
        proposal_id = _commit_trace_order[position];
        map_delete(_commit_trace_order, position);
        _commit_trace_order_head++;
        if (stringp(proposal_id)
            && _commit_trace_positions[proposal_id] == position) {
            map_delete(_commit_trace_positions, proposal_id);
            map_delete(_commit_trace, proposal_id);
        }
        _trace_evictions++;
    }
}

private void record_trace(string proposal_id, string state, string reason)
{
    int previous_position;
    int position;

    previous_position = _commit_trace_positions[proposal_id];
    if (previous_position > 0)
        map_delete(_commit_trace_order, previous_position);
    position = _commit_trace_order_tail++;
    _commit_trace_order[position] = proposal_id;
    _commit_trace_positions[proposal_id] = position;
    _commit_trace[proposal_id] = ([
        "proposal_id": proposal_id,
        "state": state,
        "reason": reason || "",
        "updated_at": time(),
    ]);
    prune_proposal_trace_history();
}

private void sweep_expired_proposals()
{
    mapping proposal;
    string proposal_id;

    foreach (proposal_id in keys(_proposals)) {
        proposal = _proposals[proposal_id];
        if (!mapp(proposal) || (proposal["expires_at"] || 0) >= time())
            continue;
        _rejected_count++;
        record_trace(proposal_id, "rejected", "expired");
        map_delete(_proposals, proposal_id);
    }
}

mapping submit_proposal(mapping proposal)
{
    string proposal_id;

    if (!mapp(proposal))
        return (["ok":0, "error":"proposal mapping is required"]);
    if (!stringp(proposal["owner_key"]) || proposal["owner_key"] == "")
        return (["ok":0, "error":"proposal owner_key is required"]);
    if (!stringp(proposal["type"]) || proposal["type"] == "")
        return (["ok":0, "error":"proposal type is required"]);

    proposal = copy(proposal);
    proposal_id = proposal["proposal_id"];
    if (!stringp(proposal_id) || proposal_id == "")
        proposal_id = make_proposal_id(proposal);
    sweep_expired_proposals();
    if (mapp(_proposals[proposal_id]))
        return (["ok":1, "duplicate":1, "proposal_id":proposal_id,
                 "proposal":copy(_proposals[proposal_id])]);
    if (mapp(_commit_trace[proposal_id]))
        return (["ok":1, "duplicate":1, "proposal_id":proposal_id,
                 "state":_commit_trace[proposal_id]["state"]]);

    if (sizeof(_proposals) >= MAX_PENDING_PROPOSALS) {
        _pending_capacity_rejections++;
        return (["ok":0, "error":"pending proposal capacity reached"]);
    }

    proposal["proposal_id"] = proposal_id;
    proposal["submitted_at"] = proposal["submitted_at"] || time();
    proposal["expires_at"] = proposal["expires_at"] || (time() + 30);
    proposal["state"] = "pending";
    _proposals[proposal_id] = proposal;
    _submitted_count++;
    record_trace(proposal_id, "pending", "");
    return (["ok":1, "proposal_id": proposal_id, "proposal": copy(proposal)]);
}

mapping validate_proposal(string proposal_id, mapping guard)
{
    mapping proposal;
    string reason;

    proposal = _proposals[proposal_id];
    if (!mapp(proposal))
        return (["ok":0, "state":"missing", "reason":"proposal missing"]);
    if ((proposal["expires_at"] || 0) < time())
        reason = "expired";
    else if (mapp(guard) && stringp(guard["owner_key"]) && guard["owner_key"] != proposal["owner_key"])
        reason = "owner_mismatch";
    else if (mapp(guard) && intp(guard["owner_epoch"]) && intp(proposal["owner_epoch"]) &&
        guard["owner_epoch"] != proposal["owner_epoch"])
        reason = "epoch_mismatch";
    else if (mapp(guard) && intp(guard["input_hash"]) && intp(proposal["input_hash"]) &&
        guard["input_hash"] != proposal["input_hash"])
        reason = "input_hash_mismatch";

    if (stringp(reason) && reason != "") {
        proposal["state"] = "rejected";
        proposal["reject_reason"] = reason;
        _rejected_count++;
        record_trace(proposal_id, "rejected", reason);
        map_delete(_proposals, proposal_id);
        return (["ok":0, "state":"rejected", "reason":reason, "proposal": copy(proposal)]);
    }

    return (["ok":1, "state":"validated", "proposal": copy(proposal)]);
}

mapping commit_proposal(string proposal_id, mapping guard, mapping commit_result)
{
    mapping validation;
    mapping proposal;

    validation = validate_proposal(proposal_id, guard);
    if (!validation["ok"])
        return validation;

    proposal = _proposals[proposal_id];
    proposal["state"] = "committed";
    proposal["committed_at"] = time();
    proposal["commit_result"] = mapp(commit_result) ? copy(commit_result) : ([]);
    _committed_count++;
    record_trace(proposal_id, "committed", "");
    map_delete(_proposals, proposal_id);
    return (["ok":1, "state":"committed", "proposal_id":proposal_id, "commit_result":proposal["commit_result"]]);
}

mapping reject_proposal(string proposal_id, string reason)
{
    mapping proposal;

    proposal = _proposals[proposal_id];
    if (!mapp(proposal))
        return (["ok":0, "state":"missing", "reason":"proposal missing"]);
    proposal["state"] = "rejected";
    proposal["reject_reason"] = reason || "rejected";
    _rejected_count++;
    record_trace(proposal_id, "rejected", proposal["reject_reason"]);
    map_delete(_proposals, proposal_id);
    return (["ok":1, "state":"rejected", "proposal_id":proposal_id, "reason":proposal["reject_reason"]]);
}

mapping query_proposal_commit_status()
{
    sweep_expired_proposals();
    return ([
        "submitted": _submitted_count,
        "committed": _committed_count,
        "rejected": _rejected_count,
        "pending": sizeof(keys(_proposals)),
        "trace": copy(_commit_trace),
        "pending_capacity_rejections": _pending_capacity_rejections,
        "trace_evictions": _trace_evictions,
        "max_pending_proposals": MAX_PENDING_PROPOSALS,
        "max_trace_history": MAX_PROPOSAL_TRACE_HISTORY,
    ]);
}
