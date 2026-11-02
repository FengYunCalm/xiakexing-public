/**
 * @file    hp.c
 * @brief   简化版生命值命令 - 仅查看自己



 *
 * @par Description:
 * 轻量生命状态命令，只展示当前角色的气血与内力快照。
 * 详细属性面板继续由 `score` 负责，本命令不承担更多状态渲染职责。
 */

#pragma strict_types

#include <system/ansi.h>
#include <game/command.h>
#include <system/globals.h>
#include <system/constants.h>
#include <system/services.h>
#include <system/xk_protocol.h>
#include <framework/message_facade.h>

inherit "/lib/framework/network";
inherit "/lib/std/utils/util_status";
inherit "/lib/std/utils/util_owner_projection";

#define HP_RENDER_CACHE_TTL 5
#define XK_PROTOCOL_CORE "/kernel/framework/xk_protocol_core"

int help(object me);
private void emit_hp_dialog(object me, string content, string worker_state,
    mapping response_context);
private string query_hp_render_signature(object me);
private string query_cached_hp_render(object me);
private mapping query_cached_hp_dialog_entry(object me);
private mapping query_cached_hp_dialog_entry_with_signature(object me,
    string signature);
private string query_cached_hp_dialog_render(object me);
private string query_cached_hp_dialog_worker_state(object me);
private void remember_hp_render(object me, string content);
private void remember_hp_dialog_render(object me, string content,
    string worker_state);
private void remember_hp_dialog_render_with_signature(object me, string content,
    string worker_state, string signature);
private int is_ready_worker_state(string worker_state);
private string render_hp_panel(object me, string worker_state);
private string render_hp_dialog(object me, string worker_state);
private mapping query_hp_projection_snapshot(object me, string worker_state);
void xk_prime_hp_dialog_cache(object me);
mapping xk_hp_dialog_extra(string worker_state);

mapping xk_hp_dialog_extra(string worker_state)
{
    mapping extra;

    extra = DIALOG_PROJECTION_D->build_dialog_extra("hp", "hp_panel", ([
        "replace_mode": "replace",
        "actions": ({
            (["label":"刷新状态", "cmd":"hp"]),
            (["label":"角色状态", "cmd":"score"]),
            (["label":"服务器状态", "cmd":"status"]),
            (["label":"查看帮助", "cmd":"help hp"]),
        }),
    ]));
    extra["read_side_worker"] = is_ready_worker_state(worker_state) ? 1 : 0;
    extra["read_side_worker_state"] = stringp(worker_state) && worker_state != ""
        ? worker_state : "pending";
    return extra;
}

private void emit_hp_dialog(object me, string content, string worker_state,
    mapping response_context)
{
    mapping extra;

    if (!objectp(me) || !stringp(content) || content == "")
        return;

    extra = xk_hp_dialog_extra(worker_state);
    xk_send_dialog(me, "生命状态面板", content, extra, response_context);
}

private string query_hp_render_signature(object me)
{
    mapping combat;

    if (!objectp(me))
        return "invalid";

    combat = query_combat_resource_snapshot(me);
    return sprintf("%d|%d|%d|%d",
        combat["hp"] || 0,
        combat["max_hp"] || 0,
        combat["mp"] || 0,
        combat["max_mp"] || 0);
}

private string query_cached_hp_render(object me)
{
    mapping cache;
    string signature;

    if (!objectp(me))
        return "";

    cache = me->query_temp("xk/hp_render_cache");
    signature = query_hp_render_signature(me);
    if (!mapp(cache)
        || !stringp(cache["content"])
        || cache["content"] == ""
        || cache["signature"] != signature
        || !intp(cache["at"])
        || time() - cache["at"] > HP_RENDER_CACHE_TTL)
        return "";

    return cache["content"];
}

private string query_cached_hp_dialog_render(object me)
{
    mapping cache;

    cache = query_cached_hp_dialog_entry(me);
    return mapp(cache) && stringp(cache["content"]) ? cache["content"] : "";
}

private mapping query_cached_hp_dialog_entry(object me)
{
    if (!objectp(me))
        return 0;

    return query_cached_hp_dialog_entry_with_signature(me,
        query_hp_render_signature(me));
}

private mapping query_cached_hp_dialog_entry_with_signature(object me,
    string signature)
{
    mapping cache;

    if (!objectp(me) || !stringp(signature) || signature == "")
        return 0;

    cache = me->query_temp("xk/hp_dialog_cache");
    if (!mapp(cache)
        || !stringp(cache["content"])
        || cache["content"] == ""
        || cache["signature"] != signature
        || !intp(cache["at"])
        || time() - cache["at"] > HP_RENDER_CACHE_TTL)
        return 0;

    return cache;
}

private string query_cached_hp_dialog_worker_state(object me)
{
    mapping cache;

    cache = query_cached_hp_dialog_entry(me);
    if (!mapp(cache))
        return "";

    return stringp(cache["read_side_worker_state"])
        ? cache["read_side_worker_state"]
        : "";
}

private void remember_hp_render(object me, string content)
{
    if (!objectp(me) || !stringp(content) || content == "")
        return;

    me->set_temp("xk/hp_render_cache", ([
        "at": time(),
        "signature": query_hp_render_signature(me),
        "content": content,
    ]));
}

private void remember_hp_dialog_render(object me, string content,
    string worker_state)
{
    remember_hp_dialog_render_with_signature(me, content, worker_state,
        query_hp_render_signature(me));
}

private void remember_hp_dialog_render_with_signature(object me, string content,
    string worker_state, string signature)
{
    if (!objectp(me) || !stringp(content) || content == "")
        return;
    if (!stringp(signature) || signature == "")
        return;

    me->set_temp("xk/hp_dialog_cache", ([
        "at": time(),
        "signature": signature,
        "read_side_worker_state": stringp(worker_state) && worker_state != ""
            ? worker_state
            : "pending",
        "content": content,
    ]));
}

private int is_ready_worker_state(string worker_state)
{
    return stringp(worker_state)
        && (worker_state == "ready"
            || worker_state == "ready_cached");
}

private string promote_cached_hp_worker_state(string worker_state)
{
    if (!stringp(worker_state) || worker_state == "")
        return "pending_cached";
    if (worker_state == "ready" || worker_state == "ready_cached")
        return "ready_cached";
    if (worker_state == "pending" || worker_state == "pending_cached")
        return "pending_cached";
    return worker_state;
}

private string render_hp_panel(object me, string worker_state)
{
    mapping combat;
    int hp;
    int max_hp;
    int mp;
    int max_mp;
    string result;

    combat = query_combat_resource_snapshot(me);
    hp = combat["hp"];
    max_hp = combat["max_hp"];
    mp = combat["mp"];
    max_mp = combat["max_mp"];

    result = "\n";
    result += HIC "┌─────────────────────────────┐\n" NOR;
    result += HIC "│" HIG "       生命状态面板       " HIC "│\n" NOR;
    result += HIC "├─────────────────────────────┤\n" NOR;
    result += sprintf(HIC "│" HIR " 【气血】%d/%d%-10s" HIC "│\n" NOR,
        hp, max_hp, "");
    result += sprintf(HIC "│" HIR " 【内力】%d/%d%-10s" HIC "│\n" NOR,
        mp, max_mp, "");
    result += sprintf(HIC "│" HIG " 【多核】%s%-14s" HIC "│\n" NOR,
        worker_state, "");
    result += HIC "└─────────────────────────────┘\n" NOR;
    result += "\n";
    return result;
}

private string render_hp_dialog(object me, string worker_state)
{
    mapping snapshot;

    snapshot = query_hp_projection_snapshot(me, worker_state);
    return render_hp_dialog_snapshot(snapshot);
}

private mapping query_hp_projection_snapshot(object me, string worker_state)
{
    return CLIENT_PROJECTION_D->query_hp_projection_snapshot(me, worker_state);
}

void xk_prime_hp_dialog_cache(object me)
{
    string worker_state;

    if (!objectp(me) || !interactive(me) || !is_xk_client(me))
        return;
    if (mapp(query_cached_hp_dialog_entry(me)))
        return;
    if (function_exists("schedule_owner_hp_read_model_refresh", me))
        me->schedule_owner_hp_read_model_refresh(1);

    worker_state = "main_thread";
    remember_hp_dialog_render(me, render_hp_dialog(me, worker_state), worker_state);
}

int main(object me, string arg)
{
    mapping dialog_cache;
    mapping dialog_template;
    mapping projection_snapshot;
    mapping owner_snapshot;
    mapping read_model;
    int xk_client;
    string result;
    string current_signature;
    string signature;
    string worker_state;
    int snapshot_started_at;
    int fallback_started_at;
    int live_token_started_at;
    int artifact_result;
    mapping response_context;

    if (!objectp(me))
        return 0;
    response_context = CLIENT_SYNC_D->capture_command_response_context(me);

    xk_client = is_xk_client(me);
    if (!xk_client) {
        result = query_cached_hp_render(me);
        if (result != "") {
            msg_send(me, result);
            return 1;
        }
    } else {
        worker_state = "main_thread";
        live_token_started_at = perf_counter_ns();
        signature = function_exists("query_owner_hp_read_model_live_token", me)
            ? me->query_owner_hp_read_model_live_token() : "";
        if (function_exists("record_owner_protocol_projection_timing", me))
            me->record_owner_protocol_projection_timing("live_token", ([
                "projection_kind": "hp_dialog",
            ]), perf_counter_ns() - live_token_started_at);
        if (signature != "" && function_exists("query_owner_read_model", me)) {
            read_model = me->query_owner_read_model(
                "hp_dialog", signature, 0);
            if (mapp(read_model) && read_model["state"] == "ready"
                && mapp(read_model["view"])
                && stringp(read_model["view"]["content"])
                && read_model["view"]["content"] != "") {
                worker_state = stringp(read_model["read_side_worker_state"])
                    ? read_model["read_side_worker_state"] : "pending";
                result = read_model["view"]["content"];
                artifact_result = call_other(XK_PROTOCOL_CORE,
                    "xk_send_preencoded_artifact", me, XK_DLOG,
                    read_model["preencoded"], response_context);
                /* L-36：0=未填充可安全回退；1/-1（已填充待对账）都已投递，
                 * 回退重发会产生重复业务包。 */
                if (intp(artifact_result) && artifact_result != 0)
                    return 1;
                emit_hp_dialog(me, result,
                    promote_cached_hp_worker_state(worker_state),
                    response_context);
                return 1;
            }
        }

        dialog_cache = signature != ""
            ? query_cached_hp_dialog_entry_with_signature(me, signature) : 0;
        if (mapp(dialog_cache)) {
            emit_hp_dialog(me,
                dialog_cache["content"],
                promote_cached_hp_worker_state(
                    stringp(dialog_cache["read_side_worker_state"])
                        ? dialog_cache["read_side_worker_state"]
                        : ""),
                response_context);
            return 1;
        }

        snapshot_started_at = perf_counter_ns();
        projection_snapshot = query_hp_projection_snapshot(me, worker_state);
        live_token_started_at = perf_counter_ns();
        current_signature = me->query_owner_hp_read_model_live_token();
        if (function_exists("record_owner_protocol_projection_timing", me))
            me->record_owner_protocol_projection_timing("live_token", ([
                "projection_kind": "hp_dialog",
            ]), perf_counter_ns() - live_token_started_at);
        if (current_signature == "" || current_signature != signature)
            signature = "";
    }

    worker_state = "main_thread";
    if (!xk_client) {
        result = render_hp_panel(me, worker_state);
        remember_hp_render(me, result);
        msg_send(me, result);
        return 1;
    }

    projection_snapshot["payload_key"] = "xk/hp-dialog-owner-projection/v1";
    projection_snapshot["projection_kind"] = "hp_dialog";
    projection_snapshot["signature"] = signature;
    if (function_exists("record_owner_protocol_projection_timing", me))
        me->record_owner_protocol_projection_timing("snapshot", projection_snapshot,
            perf_counter_ns() - snapshot_started_at);
    owner_snapshot = copy(projection_snapshot);
    owner_snapshot["read_side_worker_state"] = "ready";
    dialog_template = CLIENT_PROJECTION_D->query_hp_dialog_preencoded_template();
    if (mapp(dialog_template) && mapp(dialog_template["stable_payload"])
        && mapp(dialog_template["dynamic_defaults"])) {
        owner_snapshot["protocol_stable_payload"] =
            copy(dialog_template["stable_payload"]);
        owner_snapshot["protocol_dynamic_defaults"] =
            copy(dialog_template["dynamic_defaults"]);
    }
    if (signature != "" && mapp(dialog_template)
        && mapp(dialog_template["stable_payload"])
        && mapp(dialog_template["dynamic_defaults"])
        && function_exists("publish_read_model_artifact", me)) {
        me->publish_read_model_artifact("hp_dialog", owner_snapshot, signature,
            mapp(read_model) && read_model["state"] == "stale" ? 1 : 0);
        if (function_exists("query_owner_read_model", me)) {
            read_model = me->query_owner_read_model(
                "hp_dialog", signature, 0);
            if (mapp(read_model) && read_model["state"] == "ready"
                && mapp(read_model["view"])
                && stringp(read_model["view"]["content"])
                && read_model["view"]["content"] != "") {
                result = read_model["view"]["content"];
                worker_state = stringp(read_model["read_side_worker_state"])
                    ? read_model["read_side_worker_state"] : "ready";
                artifact_result = call_other(XK_PROTOCOL_CORE,
                    "xk_send_preencoded_artifact", me, XK_DLOG,
                    read_model["preencoded"], response_context);
                /* L-36：-1（已填充待对账）不得回退重发。 */
                if (intp(artifact_result) && artifact_result != 0)
                    return 1;
                emit_hp_dialog(me, result,
                    promote_cached_hp_worker_state(worker_state),
                    response_context);
                return 1;
            }
        }
    }

    if (function_exists("record_owner_protocol_sync_fallback", me))
        me->record_owner_protocol_sync_fallback("hp_dialog",
            "owner read model not current");
    fallback_started_at = perf_counter_ns();
    result = render_hp_dialog_snapshot(projection_snapshot);
    if (function_exists("record_owner_protocol_projection_timing", me))
        me->record_owner_protocol_projection_timing("sync_fallback",
            projection_snapshot, perf_counter_ns() - fallback_started_at);
    remember_hp_dialog_render_with_signature(me, result, worker_state, signature);
    emit_hp_dialog(me, result, worker_state, response_context);

    return 1;
}

int help(object me)
{
    msg_send(me, @HELP
命令：hp（生命状态）

查看自己的生命值与内力概况。

用法：
  hp              查看自己的生命状态

显示内容：
  - 当前气血 / 最大气血
  - 当前内力 / 最大内力

相关命令：
  score   - 查看完整角色状态
  skills  - 查看技能列表

HELP
    );
    return 1;
}
