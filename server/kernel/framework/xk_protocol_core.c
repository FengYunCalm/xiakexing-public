/**
 * @file    xk_protocol_core.c
 * @brief   XK 协议核心实现
 * @author  XiaKeXing Development Team
 * @date    2026-03-20
 * @version 1.1.0（已拆分为 xk_protocol/ 域模块）
 *
 * @par Description:
 * XK 协议核心实现，负责统一的消息编码、发送与分发辅助。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/constants.h>
#include <system/errors.h>
#include <system/services.h>
#include <system/xk_protocol.h>
#include <framework/message_facade.h>

inherit "/kernel/framework/xk_protocol/seq";
inherit "/kernel/framework/xk_protocol/meta";
inherit "/kernel/framework/xk_protocol/encode";
inherit "/kernel/framework/xk_protocol/delivery";

/* 对外公共 API 契约原型 */
int persist_protocol_counters();
int query_next_server_seq();
int reserve_contiguous_server_seq_block(int count);
mapping xk_reserve_room_message_event_sequence_block(object *targets);
int query_target_stream_epoch(object ob, string stream);
int should_auto_bump_stream(string stream);
int xk_bump_stream_epoch(object ob, string stream);
int uses_default_chat_batch_meta(mapping payload);
mapping attach_protocol_meta(object ob, string type, mapping raw_payload);
mapping *normalize_dialog_action_groups(mixed raw_groups);
mapping *normalize_dialog_action_groups_from_actions(mixed raw_actions);
void msg_send(object ob, string msg);
string encode_protocol_json(mixed value);
mapping query_xk_protocol_json_encoder_status();
mapping xk_reserve_protocol_meta(object ob, string type, mapping payload);
int xk_register_room_message_reservations(object *targets, int *reservation_ids, int *reused, int first_server_seq);
int xk_register_sparse_room_message_reservations(object *targets, int *reservation_ids, int *slot_server_seqs);
mapping xk_fill_reserved_preencoded_room_message_events(object ob, string *stable_children_json, string *scope_types, string scope_id, int *message_seqs, int *server_seqs, int *message_epochs, int *sent_ats, mapping reservation);
mapping xk_fill_reserved_native_room_message_events(object ob, string scope_id, int event_count, mapping reservation);
mapping xk_fill_reserved_native_room_message_event_entries(object *targets, mapping *entries);
mapping xk_reserve_protocol_payload(object ob, string type, mapping payload);
int xk_fill_reserved_protocol(object ob, string type, mapping raw_payload, mapping reservation);
int xk_release_reserved_protocol(object ob, mapping reservation);
int xk_release_reserved_protocol_id(object ob, int reservation_id);
string xk_encode(mapping data);
mapping xk_decode(string json);
mapping decode_preencoded_stable_payload(mapping artifact);
string combine_preencoded_object_members(string stable_json, string dynamic_json);
mapping build_map_grid_dynamic_payload(object ob);
mapping build_preencoded_dynamic_payload(object ob, string type,
    mapping artifact, mapping response_context);
int preencoded_artifact_matches(string type, mapping dynamic_payload, mapping artifact);
void record_preencoded_protocol_result(string type, mapping artifact, string outcome, int started_at);
int xk_fill_reserved_preencoded_artifact(object ob, string type, mapping artifact, mapping reservation);
int xk_send_preencoded_artifact(object ob, string type,
    mapping artifact, mapping response_context);
varargs int xk_send(object ob, string type, mapping payload,
    mapping response_context);
int xk_send_message(object ob, string text, string channel);
int xk_send_message_ex(object ob, string text, string channel, mapping extra);
int xk_send_text_packet(object ob, string text, mapping options);
varargs int xk_send_system(object ob, string text, string level,
    mapping response_context);
varargs int xk_send_error(object ob, string error, int code,
    mapping response_context);
varargs int xk_send_command_error(object ob, string error, int code,
    string command, string reason, string request, mapping response_context);
varargs int xk_send_message_event(object ob, mapping payload,
    mapping response_context);
mapping xk_build_dialog_preencoded_template(object ob, string title, string content, mapping extra);
mapping xk_build_dialog_payload(object ob, string title, string content, mapping extra);
int xk_send_dialog(object ob, string title, string content, mapping extra);
string xk_preencode_chat_batch_child(mapping payload);
varargs int xk_send_preencoded_chat_batch(object ob,
    string *stable_children_json, mapping response_context);
varargs int xk_send_batch(object ob, mixed messages,
    mapping response_context);
varargs int xk_send_report(object ob, string command, mapping data,
    mapping response_context);
varargs int xk_send_combat(object ob, string type, mapping data,
    mapping response_context);
int xk_send_heartbeat(object ob);
void xk_broadcast(string type, mapping payload, object exclude);
void xk_broadcast_message(string text, object exclude);
void xk_broadcast_system(string text, string level, object exclude);
void xk_broadcast_room(object room, string type, mapping payload, object exclude);
int xk_send_capa_response(object target, mapping data);
int xk_send_pong(object target, mixed seq);
