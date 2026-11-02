/**
 * @file    simul_efun_room_sync_xk.c
 * @brief   房间同步编排 XK 辅助子服务
 * @author  XiaKeXing Development Team
 * @date    2026-03-30
 * @version 1.0.0
 */

#pragma strict_types

#include <system/room_meta_helpers.h>
#include <system/services.h>
#include <system/xk_protocol.h>

#define ROOM_ENTITIES_XK_HELPER "/kernel/simul_efun_room_entities_xk"

private mapping batch_child(string type, mapping payload)
{
    return ([
        "type": type,
        "payload": mapp(payload) ? payload : ([]),
    ]);
}

/*
 * 房间快照完整性 envelope：navigation/complete 两种快照语义统一
 * 注入每个子包 payload 的 meta，客户端按 snapshot_kind 决定替换或保留实体；
 * room_id 与投影身份不一致的子包整批拒绝，room_snapshot_revision 只用于
 * 同一投影内的房间包排序；complete 批次还共享 transaction_id。
 */
private mapping attach_room_snapshot_meta(mapping payload, string snapshot_kind,
    object me, object room, int room_snapshot_revision,
    string transaction_id, string *complete_fields)
{
    mapping identity;
    mapping meta;

    if (!mapp(payload))
        payload = ([ ]);
    identity = LOGIN_PROJECTION_D->query_projection_identity(me);
    meta = mapp(payload["meta"]) ? copy(payload["meta"]) : ([ ]);
    meta["snapshot_kind"] = stringp(snapshot_kind) && snapshot_kind != ""
        ? snapshot_kind : "navigation";
    meta["room_id"] = objectp(room) ? base_name(room) : "";
    meta["projection_id"] = stringp(identity["projection_id"])
        ? identity["projection_id"] : "";
    meta["projection_revision"] = intp(identity["projection_revision"])
        ? identity["projection_revision"] : 0;
    meta["projection_generation"] = intp(identity["projection_generation"])
        ? identity["projection_generation"] : 0;
    if (intp(room_snapshot_revision) && room_snapshot_revision > 0)
        meta["room_snapshot_revision"] = room_snapshot_revision;
    if (stringp(transaction_id) && transaction_id != "")
        meta["transaction_id"] = transaction_id;
    if (arrayp(complete_fields) && sizeof(complete_fields) > 0)
        meta["complete_fields"] = complete_fields;
    payload["meta"] = meta;
    return payload;
}

varargs int xk_emit_room_navigation_snapshot(object me, object room,
    mapping response_context)
{
    mapping room_view;
    mapping exits;
    mixed *messages;
    string title;
    string zone;
    string room_id;
    int phase_started_at;
    int phase_cpu_started_at;
    int phase_cpu_finished_at;
    int sent;

    if (!me || !interactive(me))
        return 0;

    if (!room)
        room = environment(me);
    if (!room)
        return 0;

    phase_started_at = perf_counter_ns();
    phase_cpu_started_at = GATEWAY_TELEMETRY_D->query_gateway_thread_cpu_time_ns();
    room_view = ROOM_VIEW_D->query_room_meta_view(me, room);
    phase_cpu_finished_at = GATEWAY_TELEMETRY_D->query_gateway_thread_cpu_time_ns();
    GATEWAY_TELEMETRY_D->record_gateway_movement_phase_timing(
        me, "navigation_meta_query", perf_counter_ns() - phase_started_at,
        phase_cpu_started_at >= 0 && phase_cpu_finished_at >= phase_cpu_started_at
            ? phase_cpu_finished_at - phase_cpu_started_at : -1);
    if (!mapp(room_view))
        return 0;

    phase_started_at = perf_counter_ns();
    phase_cpu_started_at = GATEWAY_TELEMETRY_D->query_gateway_thread_cpu_time_ns();
    title = room_view["title"] || "未知地点";
    zone = room_view["zone"] || "";
    room_id = base_name(room);
    exits = mapp(room_view["exits"]) ? room_view["exits"] : ([ ]);

    messages = ({
        batch_child(XK_TITL, attach_room_snapshot_meta(
            call_other(ROOM_TITLE_XK_HELPER,
                "xk_build_room_title_payload_ex", title, zone, room_id,
                room_view["instance_kind"] || "", room_view["dungeon_type"] || "",
                room_view["dungeon_gate"] || ""),
            "navigation", me, room, 0, "", 0)),
        batch_child(XK_EXIT, attach_room_snapshot_meta(
            call_other(ROOM_EXITS_XK_HELPER,
                "xk_build_exits_payload", exits, me),
            "navigation", me, room, 0, "", 0)),
    });
    phase_cpu_finished_at = GATEWAY_TELEMETRY_D->query_gateway_thread_cpu_time_ns();
    GATEWAY_TELEMETRY_D->record_gateway_movement_phase_timing(
        me, "navigation_payload_build", perf_counter_ns() - phase_started_at,
        phase_cpu_started_at >= 0 && phase_cpu_finished_at >= phase_cpu_started_at
            ? phase_cpu_finished_at - phase_cpu_started_at : -1);

    phase_started_at = perf_counter_ns();
    phase_cpu_started_at = GATEWAY_TELEMETRY_D->query_gateway_thread_cpu_time_ns();
    sent = call_other("/kernel/simul_efun", "xk_send_batch", me, messages,
        response_context);
    phase_cpu_finished_at = GATEWAY_TELEMETRY_D->query_gateway_thread_cpu_time_ns();
    GATEWAY_TELEMETRY_D->record_gateway_movement_phase_timing(
        me, "navigation_batch_send", perf_counter_ns() - phase_started_at,
        phase_cpu_started_at >= 0 && phase_cpu_finished_at >= phase_cpu_started_at
            ? phase_cpu_finished_at - phase_cpu_started_at : -1);
    return sent ? 1 : 0;
}

varargs int xk_emit_room_snapshot(object me, object room,
    mapping response_context)
{
    mapping room_view;
    mapping exits;
    mixed *messages;
    mixed *npcs;
    object *items;
    string title;
    string zone;
    string room_id;
    int room_snapshot_revision;
    string snapshot_kind;
    string transaction_id;

    if (!me || !interactive(me))
        return 0;

    if (!room)
        room = environment(me);
    if (!room)
        return 0;

    room_view = ROOM_VIEW_D->query_room_view(me, room);
    if (!mapp(room_view))
        return 0;

    title = room_view["title"] || "未知地点";
    zone = room_view["zone"] || "";
    room_id = base_name(room);
    exits = mapp(room_view["exits"]) ? room_view["exits"] : ([]);
    npcs = arrayp(room_view["npcs"]) ? room_view["npcs"] : ({ });
    items = arrayp(room_view["items"]) ? room_view["items"] : ({ });

    /*
     * 房间视图在 query_room_view() 阶段已经完成实体补刷与投影读取。
     * 合并为单个批包可避免客户端先 TITL/EXIT 再 NPCS/ITEM 的两次状态提交。
     * 快照 revision 在同一投影内单调递增，供客户端做房间包排序。
     * 公共多人运行态下房间快照一律 complete，避免四段状态跨批次拼接。
     * revision 必须持久（set 而非 set_temp）：玩家对象在服务端重建
     * （断线重连/顶号/重启后重新加载）后 temp 归零会从 1 重新计数，
     * 客户端 isStaleCommittedSnapshot 会把新快照误判为过期整包拒绝，
     * 表现为“后端已移动、前端永不刷新”。
     */
    snapshot_kind = "complete";
    room_snapshot_revision = (me->query("xk/room_snapshot_revision") || 0) + 1;
    me->set("xk/room_snapshot_revision", room_snapshot_revision);
    transaction_id = sprintf("%s#%d", room_id, room_snapshot_revision);
    messages = ({
        batch_child(XK_TITL, attach_room_snapshot_meta(
            call_other(ROOM_TITLE_XK_HELPER,
                "xk_build_room_title_payload_ex", title, zone, room_id,
                room_view["instance_kind"] || "", room_view["dungeon_type"] || "",
                room_view["dungeon_gate"] || ""),
            snapshot_kind, me, room, room_snapshot_revision, transaction_id,
            ({ "title", "exits", "npcs", "items" }))),
        batch_child(XK_EXIT, attach_room_snapshot_meta(
            call_other(ROOM_EXITS_XK_HELPER,
                "xk_build_exits_payload", exits, me),
            snapshot_kind, me, room, room_snapshot_revision, transaction_id,
            ({ "title", "exits", "npcs", "items" }))),
        batch_child(XK_NPCS, attach_room_snapshot_meta(
            call_other(ROOM_ENTITIES_XK_HELPER,
                "xk_build_npcs_payload", npcs, me),
            snapshot_kind, me, room, room_snapshot_revision, transaction_id,
            ({ "title", "exits", "npcs", "items" }))),
        batch_child(XK_ITEM, attach_room_snapshot_meta(
            call_other(ROOM_ENTITIES_XK_HELPER,
                "xk_build_objects_payload", items, me),
            snapshot_kind, me, room, room_snapshot_revision, transaction_id,
            ({ "title", "exits", "npcs", "items" }))),
    });

    return call_other("/kernel/simul_efun", "xk_send_batch", me, messages,
        response_context) ? 1 : 0;
}
