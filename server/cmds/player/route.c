/**
 * @file    route.c
 * @brief   路线命令 - 按世界图谱自动移动到目标房间
 */

#pragma strict_types

#include <framework/message_facade.h>
#include <system/services.h>
#include <system/xk_protocol.h>

#define ROUTE_MAX_STEPS 48
#define ROUTE_PREVIEW_MAX_STEPS 20
#define XK_PROTOCOL_CORE "/kernel/framework/xk_protocol_core"

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

int main(object me, string arg);
int help(object me);

private void send_route_preview_packet(object me, string start_room,
    string target_room, mapping route, mapping request, mapping response_context)
{
    mapping payload;
    mapping projection;

    if (!objectp(me) || !interactive(me))
        return;

    request = mapp(request) ? request : ([]);
    projection = stringp(request["projection_id"])
        && request["projection_id"] != "" ? request
            : LOGIN_PROJECTION_D->query_projection_identity(me);
    payload = ([
        "protocol_version": XK_PROTOCOL_VERSION_STRING,
        "request_id": request["request_id"] || "",
        "projection_id": projection["projection_id"] || "",
        "projection_revision": projection["projection_revision"] || 0,
        "projection_generation": projection["projection_generation"] || 0,
        "source_kind": request["source_kind"] || "",
        "objective_id": request["objective_id"] || "",
        "acceptance_id": request["acceptance_id"] || "",
        "acceptance_revision": request["acceptance_revision"] || 0,
        "start_room_id": stringp(start_room) ? start_room : "",
        "target_room_id": stringp(target_room) ? target_room : "",
        "target_room_name": call_other("/kernel/simul_efun", "resolve_room_display_name", target_room),
        "ok": mapp(route) && route["ok"] ? 1 : 0,
        "error": mapp(route) && stringp(route["error"]) ? route["error"] : "",
        "commands": mapp(route) && arrayp(route["commands"]) ? route["commands"] : ({ }),
        "rooms": mapp(route) && arrayp(route["rooms"]) ? route["rooms"] : ({ }),
        "steps": mapp(route) && arrayp(route["steps"]) ? route["steps"] : ({ }),
        "transfer_count": mapp(route) && intp(route["transfer_count"])
            ? route["transfer_count"] : 0,
        "topology_generation": mapp(route) && intp(route["topology_generation"])
            ? route["topology_generation"] : 0,
        "max_steps": ROUTE_PREVIEW_MAX_STEPS,
    ]);
    call_other(XK_PROTOCOL_CORE, "xk_send", me, XK_ROUT, payload,
        response_context);
}

private mapping query_objective_projection(object me, string source_kind,
    string objective_id, string acceptance_id, string target_room)
{
    switch (source_kind) {
    case "quest":
        return QUEST_PROTOCOL->xk_query_quest_objective_projection(me,
            objective_id, acceptance_id, target_room);
    default:
        return ([ "ok": 0, "error": "unknown_source_kind" ]);
    }
}

private string query_route_error_message(string error)
{
    switch (error) {
        case "missing_room":
            return "用法：route <目标房间路径>\n";
        case "unknown_target":
            return "没有找到这个目标地点。\n";
        case "unreachable":
            return "当前位置暂时没有可达路线。\n";
        case "too_far":
            return "目标太远，请先靠近后再使用前往。\n";
        case "unknown_start":
            return "当前位置暂不支持自动寻路。\n";
        default:
            return "自动寻路失败。\n";
    }
}

private int move_by_route(object me, string target_room, int max_steps,
    mapping response_context)
{
    mapping route;
    mapping result;
    mapping step;
    mapping transition;
    object env;
    string current_room;
    string dir;
    int steps_done;
    int location_generation;

    while (steps_done < max_steps) {
        env = environment(me);
        current_room = objectp(env) ? base_name(env) : "";
        route = WORLD_MAP_D->query_route_plan(current_room, target_room,
            max_steps - steps_done);
        if (!mapp(route) || !route["ok"]) {
            msg_send(me, query_route_error_message(
                mapp(route) ? route["error"] : ""), response_context);
            return 0;
        }
        if (!arrayp(route["steps"]) || sizeof(route["steps"]) == 0)
            return 1;
        step = route["steps"][0];
        if (step["kind"] == "transfer") {
            transition = mapp(step["transition"]) ? step["transition"] : ([]);
            location_generation = me->query_temp("world/location_generation");
            result = WORLD_TRAVEL_D->move_transfer(me, transition, ([
                "expected_location_generation": intp(location_generation)
                    ? location_generation : 0,
                "expected_transfer_generation": transition["transfer_generation"] || 0,
                "idempotency_key": sprintf("route-transfer|%s|%d",
                    transition["id"] || "", steps_done),
            ]));
            if (!mapp(result) || !result["ok"]) {
                msg_send(me, stringp(result["error"]) && result["error"] != ""
                    ? result["error"] + "\n" : "区域切换失败，路线已停止。\n",
                    response_context);
                return 0;
            }
        } else {
            dir = step["direction"];
            result = move_player_by_direction(me, dir);
            if (!mapp(result) || !result["ok"]) {
                msg_send(me, mapp(result) && stringp(result["error_message"])
                    && result["error_message"] != ""
                    ? result["error_message"] : "自动寻路中断。\n", response_context);
                return 0;
            }
        }
        steps_done++;
    }
    msg_send(me, "路线超过单次自动行走步数，已停止。\n", response_context);
    return 0;
}

int main(object me, string arg)
{
    mapping route;
    object env;
    string room_path;
    string *commands;
    string acceptance_id;
    string objective_id;
    string preview_target;
    string request_id;
    string source_kind;
    mapping objective_projection;
    mapping request;
    mapping response_context;

    if (!objectp(me))
        return 0;

    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    env = environment(me);
    room_path = objectp(env) ? base_name(env) : "";
    arg = stringp(arg) ? trim(arg) : "";
    if (sscanf(arg, "preview v2 %s %s %s %s %s", request_id, source_kind,
        objective_id, acceptance_id, preview_target) == 5) {
        request_id = trim(request_id);
        source_kind = trim(source_kind);
        objective_id = trim(objective_id);
        acceptance_id = trim(acceptance_id);
        preview_target = trim(preview_target);
        request = ([
            "request_id": request_id,
            "source_kind": source_kind,
            "objective_id": objective_id,
            "acceptance_id": acceptance_id,
        ]);
        request += LOGIN_PROJECTION_D->query_projection_identity(me);
        if (request_id == "" || objective_id == "" || acceptance_id == ""
            || preview_target == "") {
            send_route_preview_packet(me, room_path, preview_target,
                ([ "ok": 0, "error": "invalid_request" ]), request,
                response_context);
            return 1;
        }
        objective_projection = query_objective_projection(me, source_kind,
            objective_id, acceptance_id, preview_target);
        if (!mapp(objective_projection) || !objective_projection["ok"]
            || !mapp(objective_projection["objective"])) {
            send_route_preview_packet(me, room_path, preview_target, ([
                "ok": 0,
                "error": mapp(objective_projection)
                    ? (objective_projection["error"] || "objective_stale")
                    : "objective_stale",
            ]), request, response_context);
            return 1;
        }
        if (objective_projection["projection_id"]
                != request["projection_id"]
            || objective_projection["projection_revision"]
                != request["projection_revision"]
            || !LOGIN_PROJECTION_D->projection_identity_matches(me, request)
            || !objectp(environment(me))
            || base_name(environment(me)) != room_path) {
            send_route_preview_packet(me, room_path, preview_target, ([
                "ok": 0,
                "error": "projection_stale",
            ]), request, response_context);
            return 1;
        }
        request["acceptance_revision"] =
            objective_projection["objective"]["acceptance_revision"] || 0;
        route = WORLD_MAP_D->query_route_commands(room_path, preview_target,
            ROUTE_PREVIEW_MAX_STEPS);
        if (!LOGIN_PROJECTION_D->projection_identity_matches(me, request)
            || !objectp(environment(me))
            || base_name(environment(me)) != room_path) {
            send_route_preview_packet(me, room_path, preview_target, ([
                "ok": 0,
                "error": "projection_stale",
            ]), request, response_context);
            return 1;
        }
        send_route_preview_packet(me, room_path, preview_target, route, request,
            response_context);
        return 1;
    }
    if (sscanf(arg, "preview %s", preview_target) == 1) {
        preview_target = trim(preview_target);
        route = WORLD_MAP_D->query_route_commands(room_path, preview_target, ROUTE_PREVIEW_MAX_STEPS);
        send_route_preview_packet(me, room_path, preview_target, route, 0,
            response_context);
        return 1;
    }

    if (me->is_fighting()) {
        msg_send(me, "战斗中不能自动寻路。\n", response_context);
        return 1;
    }
    if (me->is_busy()) {
        msg_send(me, "你正忙着呢。\n", response_context);
        return 1;
    }

    route = WORLD_MAP_D->query_route_plan(room_path, stringp(arg) ? arg : "", ROUTE_MAX_STEPS);
    if (!mapp(route) || !route["ok"]) {
        msg_send(me, query_route_error_message(mapp(route) ? route["error"] : ""),
            response_context);
        return 1;
    }

    commands = arrayp(route["commands"]) ? route["commands"] : ({ });
    if (!arrayp(route["steps"]) || sizeof(route["steps"]) == 0) {
        msg_send(me, "你已经在目标地点。\n", response_context);
        return 1;
    }

    if (move_by_route(me, stringp(arg) ? arg : "", ROUTE_MAX_STEPS,
        response_context))
        msg_send(me, sprintf("你按路线前进了 %d 步。\n", sizeof(route["steps"])),
            response_context);
    return 1;
}

int help(object me)
{
    msg_send(me, @HELP
命令：route（自动寻路）

用法：
  route <目标房间路径>
  route preview <目标房间路径>
  route preview v2 <请求ID> <目标来源> <目标ID> <接取ID> <目标房间路径>

说明：
  route 按当前内容图谱寻找可达路径并逐步移动。
  preview 只返回最多 20 步的结构化路线预览，不移动角色。
  v2 预览会校验当前投影、目标来源与接取身份，供客户端安全追踪。

HELP
    );
    return 1;
}
