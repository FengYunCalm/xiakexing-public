/**
 * @file    xk_protocol/encode.c
 * @brief   XK 协议核心 - 帧/JSON/预编码载荷编码
 *
 * @par Description:
 * 含 {xk_encode, xk_decode, encode_protocol_json, query_xk_protocol_json_encoder_status, preencoded_key_set_matches, decode_preencoded_stable_payload, combine_preencoded_object_members, build_map_grid_dynamic_payload, build_preencoded_dynamic_payload, preencoded_artifact_matches, record_preencoded_protocol_result, query_next_dialog_seq, xk_build_dialog_payload, xk_build_dialog_preencoded_template, xk_preencode_chat_batch_child}。
 *
 * 本模块是 encode 域协议片段，由 `xk_protocol_core.c` 继承；
 * 只有本簇声明的 nosave 状态归属本簇，跨簇调用走 public 原型。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/constants.h>
#include <system/errors.h>
#include <system/services.h>
#include <system/xk_protocol.h>
#include <framework/message_facade.h>

private nosave int _xk_protocol_json_native_count;
private nosave int _xk_protocol_json_legacy_fallback_count;
/* 跨簇调用原型 */
mapping *normalize_dialog_action_groups(mixed raw_groups);
mapping *normalize_dialog_action_groups_from_actions(mixed raw_actions);
int uses_default_chat_batch_meta(mapping payload);

private int query_next_dialog_seq(object ob)
{
    mixed seq;

    if (!objectp(ob))
        return 0;

    catch(seq = ob->query_temp("protocol/dialog_seq"));
    if (!intp(seq) || seq < 0)
        seq = 0;

    seq += 1;
    catch(ob->set_temp("protocol/dialog_seq", seq));

    return seq;
}
string encode_protocol_json(mixed value)
{
    string encoded;

    encoded = json_encode_frozen_or_zero(value);
    if (stringp(encoded)) {
        _xk_protocol_json_native_count++;
        return encoded;
    }
    _xk_protocol_json_legacy_fallback_count++;
    return json_encode(value);
}
mapping query_xk_protocol_json_encoder_status()
{
    return ([
        "xk_protocol_json_native_count": _xk_protocol_json_native_count,
        "xk_protocol_json_legacy_fallback_count":
            _xk_protocol_json_legacy_fallback_count,
    ]);
}
string xk_encode(mapping data)
{
    if (!mapp(data))
        return "";

    return encode_protocol_json(data);
}
mapping xk_decode(string json)
{
    mapping data;
    mixed err;

    if (!stringp(json) || json == "")
        return 0;

    err = catch(data = json_decode(json));
    if (err) {
        return 0;
    }

    return mapp(data) ? data : 0;
}
private int preencoded_key_set_matches(mapping payload, mixed declared_keys)
{
    mixed key;
    mixed *actual_keys;

    if (!mapp(payload) || !arrayp(declared_keys))
        return 0;
    actual_keys = keys(payload);
    if (sizeof(actual_keys) != sizeof(declared_keys))
        return 0;
    foreach (key in actual_keys) {
        if (!stringp(key) || member_array(key, declared_keys) == -1)
            return 0;
    }
    foreach (key in declared_keys) {
        if (!stringp(key) || member_array(key, actual_keys) == -1)
            return 0;
    }
    return 1;
}
mapping decode_preencoded_stable_payload(mapping artifact)
{
    mapping payload;
    mixed err;

    if (!mapp(artifact) || !stringp(artifact["stable_json"])
        || !arrayp(artifact["stable_keys"]))
        return 0;
    err = catch(payload = json_decode(artifact["stable_json"]));
    if (err || !mapp(payload)
        || !preencoded_key_set_matches(payload, artifact["stable_keys"]))
        return 0;
    return payload;
}
string combine_preencoded_object_members(string stable_json,
    string dynamic_json)
{
    string stable_members;
    string dynamic_members;

    if (!stringp(stable_json) || strlen(stable_json) < 2
        || stable_json[0] != '{' || stable_json[<1] != '}'
        || !stringp(dynamic_json) || strlen(dynamic_json) < 2
        || dynamic_json[0] != '{' || dynamic_json[<1] != '}')
        return "";
    stable_members = stable_json[1..<2];
    dynamic_members = dynamic_json[1..<2];
    if (stable_members == "")
        return dynamic_json;
    if (dynamic_members == "")
        return stable_json;
    return "{" + stable_members + "," + dynamic_members + "}";
}
mapping build_map_grid_dynamic_payload(object ob)
{
    mapping identity;
    mapping payload;
    object room;
    mixed raw_revision;
    mixed err;
    int map_revision;

    payload = ([
        "protocol_version": XK_PROTOCOL_VERSION_STRING,
        "projection_id": "",
        "projection_revision": 0,
        "projection_generation": 0,
        "room_id": "",
        "map_revision": 1,
    ]);
    if (!objectp(ob))
        return payload;

    identity = ([]);
    err = catch(identity = LOGIN_PROJECTION_D->query_projection_identity(ob));
    if (err || !mapp(identity))
        identity = ([]);
    room = environment(ob);

    raw_revision = 0;
    err = catch(raw_revision = ob->query_temp("xk/map_revision"));
    map_revision = !err && intp(raw_revision) && raw_revision > 0
        ? raw_revision + 1 : 1;
    catch(ob->set_temp("xk/map_revision", map_revision));

    payload["projection_id"] = stringp(identity["projection_id"])
        ? identity["projection_id"] : "";
    payload["projection_revision"] = intp(identity["projection_revision"])
        ? identity["projection_revision"] : 0;
    /* MAPS 是投影作用域帧（客户端按 ProjectionIdentity 校验，缺任一字段即拒收），
     * 必须与 REAL/QUES 一样携带内容代。 */
    payload["projection_generation"] = intp(identity["projection_generation"])
        ? identity["projection_generation"] : 0;
    payload["room_id"] = objectp(room) ? base_name(room) : "";
    payload["map_revision"] = map_revision;
    return payload;
}
mapping build_preencoded_dynamic_payload(object ob, string type,
    mapping artifact, mapping response_context)
{
    mapping dynamic_payload;
    mapping requested;
    string response_id;

    dynamic_payload = ([]);
    requested = mapp(artifact) && mapp(artifact["dynamic_defaults"])
        ? artifact["dynamic_defaults"] : ([]);
    if (type == XK_MAPS) {
        dynamic_payload = build_map_grid_dynamic_payload(ob);
    } else if (type == XK_DLOG) {
        dynamic_payload["part_index"] = intp(requested["part_index"])
            && requested["part_index"] > 0 ? requested["part_index"] : 1;
        dynamic_payload["part_total"] = intp(requested["part_total"])
            && requested["part_total"] >= dynamic_payload["part_index"]
                ? requested["part_total"] : dynamic_payload["part_index"];
        dynamic_payload["final"] = undefinedp(requested["final"])
            ? (dynamic_payload["part_index"] == dynamic_payload["part_total"] ? 1 : 0)
            : (requested["final"] ? 1 : 0);
        dynamic_payload["replace_mode"] = stringp(requested["replace_mode"])
            && requested["replace_mode"] != ""
                ? requested["replace_mode"] : "replace";
        dynamic_payload["seq"] = query_next_dialog_seq(ob);
    }
    response_id = mapp(response_context)
        ? response_context["command_request_id"] : "";
    if (stringp(response_id) && response_id != "")
        dynamic_payload["command_request_id"] = response_id;
    else
        dynamic_payload["suppress_command_request_id"] = 1;
    return dynamic_payload;
}
int preencoded_artifact_matches(string type, mapping dynamic_payload,
    mapping artifact)
{
    mixed key;

    if (!mapp(artifact)
        || artifact["schema"] != "xk/preencoded-payload-members/v1"
        || artifact["producer"] != "owner_project_protocol_payload/v1"
        || artifact["validated"] != 1
        || artifact["protocol_type"] != type
        || !stringp(artifact["stable_json"])
        || strlen(artifact["stable_json"]) < 2
        || artifact["stable_json"][0] != '{'
        || artifact["stable_json"][<1] != '}'
        || !arrayp(artifact["stable_keys"]))
        return 0;
    foreach (key in keys(dynamic_payload)) {
        if (member_array(key, artifact["stable_keys"]) != -1)
            return 0;
    }
    return 1;
}
void record_preencoded_protocol_result(string type, mapping artifact,
    string outcome, int started_at)
{
    string projection_kind;
    mixed err;

    projection_kind = mapp(artifact) && stringp(artifact["projection_kind"])
        ? artifact["projection_kind"] : "";
    if ((projection_kind == "inventory" && type != XK_IVTY)
        || (projection_kind == "map_grid" && type != XK_MAPS)
        || ((projection_kind == "hp_dialog" || projection_kind == "score_dialog")
            && type != XK_DLOG))
        projection_kind = "";
    if (projection_kind == "" || !stringp(outcome) || outcome == "")
        return;
    err = catch(GATEWAY_SESSION_D->record_owner_protocol_projection(
        projection_kind, outcome));
    if (err)
        debug_message(sprintf("[xk_protocol_core] preencoded outcome failed kind=%s outcome=%s err=%O",
            projection_kind, outcome, err));
    if (started_at > 0) {
        err = catch(GATEWAY_SESSION_D->record_owner_protocol_projection_timing(
            projection_kind, "preencoded_send", perf_counter_ns() - started_at));
        if (err)
            debug_message(sprintf("[xk_protocol_core] preencoded timing failed kind=%s err=%O",
                projection_kind, err));
    }
}
mapping xk_build_dialog_preencoded_template(object ob, string title,
    string content, mapping extra)
{
    mapping dynamic_defaults;
    mapping stable_payload;
    int part_index;
    int part_total;

    if (!objectp(ob))
        return 0;

    stable_payload = mapp(extra) ? copy(extra) : ([]);
    stable_payload["title"] = stringp(title) ? title : "";
    stable_payload["content"] = stringp(content) ? content : "";
    if (!intp(stable_payload["schema_version"])
        || stable_payload["schema_version"] <= 0)
        stable_payload["schema_version"] = XK_DIALOG_SCHEMA_VERSION;
    if (!stringp(stable_payload["source"]) || stable_payload["source"] == "")
        stable_payload["source"] = "dialog";
    if (!stringp(stable_payload["dialog_id"]) || stable_payload["dialog_id"] == "")
        stable_payload["dialog_id"] = stable_payload["source"];
    stable_payload["action_groups"] = normalize_dialog_action_groups(
        stable_payload["action_groups"]);
    if (!sizeof(stable_payload["action_groups"]))
        stable_payload["action_groups"] = normalize_dialog_action_groups_from_actions(
            stable_payload["actions"]);
    map_delete(stable_payload, "actions");
    map_delete(stable_payload, "buttons");
    /* Request correlation is per command invocation, never stable artifact data. */
    map_delete(stable_payload, "command_request_id");
    map_delete(stable_payload, "response_context");
    map_delete(stable_payload, "suppress_command_request_id");

    part_index = intp(stable_payload["part_index"])
        && stable_payload["part_index"] > 0 ? stable_payload["part_index"] : 1;
    part_total = intp(stable_payload["part_total"])
        && stable_payload["part_total"] >= part_index
            ? stable_payload["part_total"] : part_index;
    dynamic_defaults = ([
        "part_index": part_index,
        "part_total": part_total,
        "final": undefinedp(stable_payload["final"])
            ? (part_index == part_total ? 1 : 0)
            : (stable_payload["final"] ? 1 : 0),
        "replace_mode": stringp(stable_payload["replace_mode"])
            && stable_payload["replace_mode"] != ""
                ? stable_payload["replace_mode"] : "replace",
    ]);
    if (mapp(stable_payload["meta"]))
        dynamic_defaults["meta"] = copy(stable_payload["meta"]);

    map_delete(stable_payload, "seq");
    map_delete(stable_payload, "part_index");
    map_delete(stable_payload, "part_total");
    map_delete(stable_payload, "final");
    map_delete(stable_payload, "replace_mode");
    map_delete(stable_payload, "meta");
    return ([
        "stable_payload": stable_payload,
        "dynamic_defaults": dynamic_defaults,
    ]);
}
mapping xk_build_dialog_payload(object ob, string title, string content,
    mapping extra)
{
    mapping payload;
    mapping template;
    mixed requested_seq;

    requested_seq = mapp(extra) ? extra["seq"] : 0;
    template = xk_build_dialog_preencoded_template(ob, title, content, extra);
    if (!mapp(template) || !mapp(template["stable_payload"])
        || !mapp(template["dynamic_defaults"]))
        return 0;
    payload = copy(template["stable_payload"]) + copy(template["dynamic_defaults"]);
    payload["seq"] = intp(requested_seq) && requested_seq > 0
        ? requested_seq : query_next_dialog_seq(ob);
    return payload;
}
string xk_preencode_chat_batch_child(mapping payload)
{
    mapping stable_payload;

    if (!uses_default_chat_batch_meta(payload))
        return "";
    stable_payload = copy(payload);
    map_delete(stable_payload, "meta");
    return encode_protocol_json(stable_payload);
}
