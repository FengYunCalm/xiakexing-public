/**
 * @file    combat_d.c
 * @brief   战斗守护进程



 *
 * @par Description:
 * 当前战斗系统真源，负责战斗建立、回合推进、伤害结算与协议同步。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/ansi.h>
#include <system/services.h>
#include <system/xk_protocol.h>
#include <game_constants.h>
#include <framework/message_facade.h>

inherit "/lib/framework/daemon";
inherit "/daemons/services/gameplay/combat/combat_rewards";

#define COMBAT_MATH_HELPER       "/daemons/services/gameplay/combat/combat_math"
#define COMBAT_ATTACK_HELPER     "/daemons/services/gameplay/combat/combat_attack_resolution"
#define COMBAT_FLEE_HELPER       "/daemons/services/gameplay/combat/combat_flee"
#define COMBAT_MESSAGING_HELPER  "/daemons/services/gameplay/combat/combat_messaging"
#define COMBAT_NPC_SKILLS_HELPER "/daemons/services/gameplay/combat/combat_npc_skills"
#define COMBAT_OUTCOME_HELPER    "/daemons/services/gameplay/combat/combat_outcome"
#define COMBAT_ROUNDS_HELPER     "/daemons/services/gameplay/combat/combat_rounds"
#define COMBAT_SESSION_HELPER    "/daemons/services/gameplay/combat/combat_session"
#define COMBAT_STATE_HELPER      "/daemons/services/gameplay/combat/combat_state"
#define COMBAT_TARGETING_HELPER  "/daemons/services/gameplay/combat/combat_targeting"
#define COMBAT_THREAT_HELPER     "/daemons/services/gameplay/combat/combat_threat"
#define DIRECTION_UTIL "/lib/std/utils/util_direction"

nosave mapping _combats;
nosave mapping _combat_id_map;
nosave mapping _xiang_counter;
nosave mapping _combat_packet_batches;
nosave int _combat_sequence;
nosave int _combat_batch_depth;

void process_round(string combat_id, int timer_generation);
void stop_combat_by_id(string combat_id);
private void send_damage_packets(object attacker, object defender, int final_damage,
    int defender_hp, mapping details);
private void send_hit_messages(object attacker, object defender, int final_damage,
    mapping details);
private void clear_stale_fight_state(object who);
private int combat_reached_round_limit(mapping combat);
int maybe_trigger_npc_combat_skill(object attacker, object defender, int nonlethal);
void clear_pending_npc_combat_skill(object actor);
private void send_flee_end_packets(object who, object opponent);
private void begin_combat_packet_batch();
private void flush_combat_packet_batch();
private int queue_combat_packet(object who, string type, mapping payload);
private mixed *append_combat_runtime_snapshot(object target, mixed *messages);
void send_combat_runtime_snapshot(object who);
int attempt_flee(object who, string direction);
void grant_npc_defeat_rewards(object npc, object killer);
object prepare_attack(object attacker, string target_name);
object prepare_fight(object attacker, string target_name);
private string resolve_active_combat_id(object who);
private void purge_stale_combats();
int is_in_combat(object who);
private void record_combat_phase(object who, string phase, int wall_started_at,
    int cpu_started_at);

private void record_combat_phase(object who, string phase, int wall_started_at,
    int cpu_started_at)
{
    int cpu_finished_at;

    cpu_finished_at = thread_cpu_time_ns();
    GATEWAY_TELEMETRY_D->record_gateway_combat_phase_timing(
        who, phase, perf_counter_ns() - wall_started_at,
        cpu_started_at >= 0 && cpu_finished_at >= cpu_started_at
            ? cpu_finished_at - cpu_started_at : -1);
}

mapping query_combat_owner_route(string key)
{
    mapping route;
    string route_key;
    mixed err;

    route_key = stringp(key) && key != "" ? key : "combat";
    err = catch(route = OWNER_RUNTIME_D->query_service_owner_route("combat", route_key));
    if (err || !mapp(route) || route["ok"] != 1)
        return ([
            "ok": 0,
            "service": "combat",
            "key": route_key,
            "failure_reason": err ? sprintf("owner route query failed: %O", err)
                : (mapp(route) ? route["failure_reason"] : "owner route unavailable"),
            "readiness": mapp(route) ? route["readiness"] : ([ "ok": 0 ]),
            "dependencies": mapp(route) ? route["dependencies"] : ([ ]),
            "direct_cross_owner_write": 0,
        ]);

    route["commit_boundary"] = "service_owner_combat_commit";
    route["direct_cross_owner_write"] = 0;
    return route;
}

mapping query_combat_owner_status()
{
    mapping route;

    route = query_combat_owner_route("combat");
    if (!mapp(route) || route["ok"] != 1)
        return route;
    return ([
        "service_map_v2": 1,
        "service": "combat",
        "policy": route["policy"],
        "owner_id": route["owner_id"],
        "active_combats": mapp(_combats) ? sizeof(_combats) : 0,
        "commit_boundary_required": 1,
        "commit_boundary": route["commit_boundary"],
        "service_shard_owner": route["service_shard_owner"] ? 1 : 0,
        "service_shard_key_required": route["service_shard_key_required"] ? 1 : 0,
        "keyed_service_shard_ready": route["keyed_service_shard_ready"] ? 1 : 0,
        "hot_path_service_owner_single_point": 0,
        "timer_callback_policy": "delayed_callback_owner_route",
        "message_only_cross_owner": 1,
        "direct_cross_owner_write": 0,
    ]);
}

private void recover_after_spar(object who)
{
    call_other(COMBAT_OUTCOME_HELPER, "recover_after_spar", who);
}

void combat_msg(object who, string text)
{
    call_other(COMBAT_MESSAGING_HELPER, "combat_msg", who, text);
}

void combat_packet(object who, string type, mapping payload)
{
    if (queue_combat_packet(who, type, payload))
        return;

    call_other(COMBAT_MESSAGING_HELPER, "combat_packet", who, type, payload);
}

private string query_combat_batch_key(object who)
{
    if (!objectp(who))
        return "";

    return file_name(who);
}

private mapping build_combat_batch_child(string type, mapping payload)
{
    mapping data;

    data = mapp(payload) ? copy(payload) : ([]);
    data["timestamp"] = time();
    return ([
        "type": type,
        "payload": data,
    ]);
}

private void begin_combat_packet_batch()
{
    _combat_batch_depth++;
    if (!mapp(_combat_packet_batches))
        _combat_packet_batches = ([]);
}

void begin_action_packet_batch()
{
    begin_combat_packet_batch();
}

private int queue_combat_packet(object who, string type, mapping payload)
{
    string key;
    mapping entry;

    /* XK_CBST（战斗开始）有意不入批：权威开局快照须即时到达，不随回合尾
     * 批量延后；后续 CBRT 快照子帧由 append_combat_runtime_snapshot 补齐。 */
    if (_combat_batch_depth <= 0 || !objectp(who) || !stringp(type) || type == XK_CBST)
        return 0;

    key = query_combat_batch_key(who);
    if (key == "")
        return 0;

    if (!mapp(_combat_packet_batches))
        _combat_packet_batches = ([]);
    entry = mapp(_combat_packet_batches[key]) ? _combat_packet_batches[key] : ([
        "target": who,
        "messages": ({ }),
    ]);
    entry["target"] = who;
    entry["messages"] += ({ build_combat_batch_child(type, payload) });
    _combat_packet_batches[key] = entry;
    return 1;
}

private mixed *append_combat_runtime_snapshot(object target, mixed *messages)
{
    mapping payload;

    if (!objectp(target) || !arrayp(messages) || sizeof(messages) == 0)
        return messages;

    payload = call_other(COMBAT_MESSAGING_HELPER, "build_combat_runtime_snapshot", target);
    if (!mapp(payload) || sizeof(keys(payload)) == 0)
        return messages;
    return messages + ({ build_combat_batch_child(XK_CBRT, payload) });
}

void send_combat_runtime_snapshot(object who)
{
    mapping payload;

    if (!objectp(who))
        return;

    payload = call_other(COMBAT_MESSAGING_HELPER, "build_combat_runtime_snapshot", who);
    if (!mapp(payload) || sizeof(keys(payload)) == 0)
        return;
    call_other(COMBAT_MESSAGING_HELPER, "combat_packet", who, XK_CBRT, payload);
}

private void flush_combat_packet_batch()
{
    mapping batches;
    string key;

    _combat_batch_depth = (_combat_batch_depth - 1);
    if (_combat_batch_depth > 0)
        return;

    _combat_batch_depth = 0;
    batches = mapp(_combat_packet_batches) ? _combat_packet_batches : ([]);
    _combat_packet_batches = ([]);

    foreach (key in keys(batches)) {
        mapping entry;
        object target;
        mixed *messages;

        entry = mapp(batches[key]) ? batches[key] : ([]);
        target = entry["target"];
        messages = arrayp(entry["messages"]) ? entry["messages"] : ({ });
        if (!objectp(target) || sizeof(messages) == 0)
            continue;
        messages = append_combat_runtime_snapshot(target, messages);
        if (sizeof(messages) == 1) {
            mapping single;

            single = mapp(messages[0]) ? messages[0] : ([]);
            call_other(COMBAT_MESSAGING_HELPER, "combat_packet", target,
                single["type"], single["payload"]);
            continue;
        }
        /* B0 双路径语义收敛：批量出口同样受 can_deliver 门控并逐子包
         * 补 MESSAGE_D combat 事件，与单发 combat_packet 语义一致。 */
        call_other(COMBAT_MESSAGING_HELPER, "deliver_combat_batch", target, messages);
    }
}

void flush_action_packet_batch()
{
    flush_combat_packet_batch();
}

private string generate_combat_id()
{
    _combat_sequence++;
    return sprintf("combat_%d_%d", time(), _combat_sequence);
}

void send_damage_packets(object attacker, object defender, int final_damage,
    int defender_hp, mapping details)
{
    call_other(COMBAT_MESSAGING_HELPER, "send_damage_packets",
        attacker, defender, final_damage, defender_hp, details);
}

void send_hit_messages(object attacker, object defender, int final_damage,
    mapping details)
{
    call_other(COMBAT_MESSAGING_HELPER, "send_hit_messages",
        attacker, defender, final_damage, details);
}

private void clear_stale_fight_state(object who)
{
    object opponent;

    if (!objectp(who))
        return;

    opponent = who->query_temp("fight_ob");
    if (objectp(opponent) && function_exists("remove_enemy", who))
        who->remove_enemy(opponent);
    who->delete_temp("fight_ob");
    who->delete_temp("enemies");
    who->delete_temp("combat/tempo/attack_progress_bp");
}

private int combat_reached_round_limit(mapping combat)
{
    int max_rounds;

    if (!mapp(combat))
        return 0;

    max_rounds = NUMERIC_RULE_D->query_combat_max_rounds();
    return max_rounds > 0 && (combat["round"] || 0) >= max_rounds;
}

private int can_reengage_combat(object who)
{
    mapping state;

    if (!objectp(who))
        return 0;

    state = NUMERIC_RULE_D->query_combat_reengage_state(who, ([ "result": "start" ]));
    return (!mapp(state) || state["allowed"]) ? 1 : 0;
}

int maybe_trigger_npc_combat_skill(object attacker, object defender, int nonlethal)
{
    return call_other(COMBAT_NPC_SKILLS_HELPER,
        "maybe_trigger_npc_combat_skill", attacker, defender, nonlethal);
}

void clear_pending_npc_combat_skill(object actor)
{
    call_other(COMBAT_NPC_SKILLS_HELPER, "clear_pending_npc_combat_skill", actor);
}

private void send_flee_end_packets(object who, object opponent)
{
    call_other(COMBAT_MESSAGING_HELPER, "send_flee_end_packets", who, opponent);
}

object prepare_attack(object attacker, string target_name)
{
    return call_other(COMBAT_TARGETING_HELPER, "prepare_attack", this_object(), attacker, target_name);
}

object prepare_fight(object attacker, string target_name)
{
    return call_other(COMBAT_TARGETING_HELPER, "prepare_fight", this_object(), attacker, target_name);
}

varargs int query_attack(object who, object target)
{
    return call_other(COMBAT_MATH_HELPER, "query_attack", who, target);
}

varargs int query_defense(object who, object target)
{
    return call_other(COMBAT_MATH_HELPER, "query_defense", who, target);
}

varargs int query_parry(object who, object target)
{
    return call_other(COMBAT_MATH_HELPER, "query_parry", who, target);
}

int calculate_hit_rate(object attacker, object defender)
{
    return call_other(COMBAT_MATH_HELPER, "calculate_hit_rate", attacker, defender);
}

int calculate_damage(object attacker, object defender)
{
    return COMBAT_DAMAGE_D->calculate_damage(attacker, defender);
}

void clear_derived_attribute_cache()
{
    call_other(COMBAT_MATH_HELPER, "clear_derived_attribute_cache");
}

void consume_equipment_durability(object attacker, object defender)
{
    call_other(COMBAT_MATH_HELPER, "consume_equipment_durability", attacker, defender);
}

private int apply_parry(object attacker, object defender, int damage)
{
    return call_other(COMBAT_ATTACK_HELPER, "apply_parry", attacker, defender, damage);
}

private void set_combat_id(object who, string combat_id)
{
    call_other(COMBAT_STATE_HELPER, "set_combat_id", _combat_id_map, who, combat_id);
}

private void clear_combat_id(object who)
{
    call_other(COMBAT_STATE_HELPER, "clear_combat_id", _combat_id_map, who);
}

private string resolve_active_combat_id(object who)
{
    return call_other(COMBAT_STATE_HELPER, "resolve_active_combat_id",
        this_object(), _combats, _combat_id_map, who);
}

private void purge_stale_combats()
{
    call_other(COMBAT_STATE_HELPER, "purge_stale_combats",
        this_object(), _combats, _combat_id_map);
}

private void sync_fight_state(object attacker, object defender)
{
    call_other(COMBAT_STATE_HELPER, "sync_fight_state", attacker, defender);
}

private void clear_fight_state(object attacker, object defender)
{
    call_other(COMBAT_STATE_HELPER, "clear_fight_state", attacker, defender);
}

void schedule_next_round(string combat_id)
{
    mapping combat;
    int timer_handle;
    int delay;

    combat = _combats[combat_id];
    if (!mapp(combat))
        return;

    /*
     * 每次调度下一回合前都先取消旧 timer，
     * 避免同一 combat_id 因重复排程而并行执行多个 round。
     * 这是战斗链稳定性的关键约束之一。
     */
    if (combat["timer"])
        TIMER_D->cancel_timer(combat["timer"]);

    combat["timer_generation"] = (combat["timer_generation"] || 0) + 1;
    delay = NUMERIC_RULE_D->query_combat_round_interval_seconds();
    combat["round_due_ns"] = perf_counter_ns() + delay * 1000000000;
    timer_handle = TIMER_D->call_later_walltime_background("process_round",
        delay, this_object(),
        combat_id, combat["timer_generation"]);
    combat["timer"] = timer_handle;
    _combats[combat_id] = combat;
}

int do_single_attack(object attacker, object defender, int nonlethal)
{
    int result;
    mixed err;

    begin_combat_packet_batch();
    call_other(COMBAT_MATH_HELPER, "begin_derived_attribute_cache");
    err = catch(result = call_other(COMBAT_ATTACK_HELPER, "do_single_attack",
        this_object(), attacker, defender, nonlethal));
    call_other(COMBAT_MATH_HELPER, "end_derived_attribute_cache");
    flush_combat_packet_batch();
    if (err) {
        log_file("combat", sprintf("[%s] do_single_attack failed: %O\n",
            ctime(time())[4..19], err));
        error(sprintf("do_single_attack failed: %O", err));
    }
    return result;
}

void end_combat(string combat_id, string result)
{
    mixed err;

    err = catch(call_other(COMBAT_SESSION_HELPER, "end_combat_session",
        _combats, _combat_id_map, combat_id, result));
    if (err) {
        call_other(COMBAT_SESSION_HELPER, "abort_combat_session",
            _combats, _combat_id_map, combat_id);
        error(sprintf("end_combat failed: %O\n", err));
    }
}

void process_round(string combat_id, int timer_generation)
{
    mapping combat;
    object attacker;
    object defender;
    int nonlethal;
    int round_due_ns;
    int round_started_at;
    int round_cpu_started_at;
    int due_lag_cpu_started_at;
    mixed err;

    combat = _combats[combat_id];
    if (!mapp(combat))
        return;
    if (timer_generation != (combat["timer_generation"] || 0))
        return;

    attacker = combat["attacker"];
    defender = combat["defender"];
    nonlethal = !combat["kill_mode"];

    if (!objectp(attacker) || !objectp(defender)) {
        end_combat(combat_id, COMBAT_RESULT_DRAW);
        return;
    }

    if (environment(attacker) != environment(defender)) {
        end_combat(combat_id, COMBAT_RESULT_DRAW);
        return;
    }

    if (combat_reached_round_limit(combat)) {
        end_combat(combat_id, COMBAT_RESULT_DRAW);
        return;
    }

    round_due_ns = intp(combat["round_due_ns"]) ? combat["round_due_ns"] : 0;
    if (round_due_ns > 0) {
        due_lag_cpu_started_at = thread_cpu_time_ns();
        record_combat_phase(attacker, "round_due_lag",
            round_due_ns, due_lag_cpu_started_at);
    }
    round_started_at = perf_counter_ns();
    round_cpu_started_at = thread_cpu_time_ns();
    combat["round"]++;
    combat["last_action"] = time();
    /* 多人战斗仇恨衰减（每回合 ×95%） */
    call_other(COMBAT_THREAT_HELPER, "decay_combat_threat", combat["threat"]);
    _combats[combat_id] = combat;
    begin_combat_packet_batch();
    call_other(COMBAT_MATH_HELPER, "begin_derived_attribute_cache");
    err = catch(call_other(COMBAT_ROUNDS_HELPER, "run_round",
        this_object(), combat_id, attacker, defender, combat["round"], nonlethal));
    call_other(COMBAT_MATH_HELPER, "end_derived_attribute_cache");
    flush_combat_packet_batch();
    record_combat_phase(attacker, "round_total", round_started_at,
        round_cpu_started_at);
    if (err) {
        log_file("combat", sprintf("[%s] process_round failed combat=%s round=%d: %O\n",
            ctime(time())[4..19], combat_id, combat["round"], err));
        call_other(COMBAT_SESSION_HELPER, "abort_combat_session",
            _combats, _combat_id_map, combat_id);
    }
}

void setup()
{
    _combats = ([]);
    _combat_id_map = ([]);
    _xiang_counter = ([]);
    _combat_packet_batches = ([]);
    _combat_sequence = 0;
    _combat_batch_depth = 0;
    log_file("combat", sprintf("[%s] Combat daemon v4.0 initialized\n", ctime(time())[4..19]));
}

void create()
{
    ::create();
    setup();

    load_object(COMBAT_MATH_HELPER);
    load_object(COMBAT_DAMAGE_D);
    load_object(COMBAT_ATTACK_HELPER);
    load_object(COMBAT_EFFECT_D);
    load_object(COMBAT_FLEE_HELPER);
    load_object(COMBAT_MESSAGING_HELPER);
    load_object(COMBAT_NPC_SKILLS_HELPER);
    load_object(COMBAT_OUTCOME_HELPER);
    load_object(COMBAT_ROUNDS_HELPER);
    load_object(COMBAT_SESSION_HELPER);
    load_object(COMBAT_STATE_HELPER);
    load_object(COMBAT_TARGETING_HELPER);
}

mapping query_all_combats()
{
    return call_other(COMBAT_STATE_HELPER, "query_all_combats",
        this_object(), _combats, _combat_id_map);
}

mapping query_combat(string combat_id)
{
    if (!stringp(combat_id))
        return 0;

    return copy(_combats[combat_id]);
}

mapping query_combat_stats()
{
    purge_stale_combats();
    return ([
        "active_combats": sizeof(_combats),
        "total_combats": _combat_sequence,
        "xiang_usage": copy(_xiang_counter)
    ]);
}

int is_in_combat(object who)
{
    return call_other(COMBAT_STATE_HELPER, "is_in_combat",
        this_object(), _combats, _combat_id_map, who);
}

string query_combat_id(object who)
{
    return call_other(COMBAT_STATE_HELPER, "query_combat_id",
        this_object(), _combats, _combat_id_map, who);
}

string start_combat(object attacker, object defender, int kill_mode)
{
    string combat_id;

    if (!objectp(attacker) || !objectp(defender) || attacker == defender)
        return 0;

    purge_stale_combats();

    if (is_in_combat(attacker) || is_in_combat(defender))
        return 0;

    if (!can_reengage_combat(attacker) || !can_reengage_combat(defender))
        return 0;

    combat_id = generate_combat_id();
    call_other(COMBAT_SESSION_HELPER, "start_combat_session",
        _combats, _combat_id_map, combat_id, attacker, defender, kill_mode);
    schedule_next_round(combat_id);
    return combat_id;
}

varargs int start_fight(object attacker, object defender, int is_kill)
{
    string combat_id;

    if (!objectp(attacker) || !objectp(defender) || attacker == defender)
        return 0;

    combat_id = query_combat_id(attacker);
    if (stringp(combat_id) && combat_id == query_combat_id(defender)) {
        if (is_kill)
            call_other(COMBAT_SESSION_HELPER, "upgrade_kill_mode", _combats, combat_id);
        return 1;
    }

    combat_id = start_combat(attacker, defender, is_kill);
    if (!stringp(combat_id))
        return 0;

    if (is_kill)
        call_other(COMBAT_SESSION_HELPER, "upgrade_kill_mode", _combats, combat_id);

    return 1;
}

/* ============ 多人战斗（仇恨值） ============ */

/* 伤害仇恨记录（由 combat_attack_resolution 在造成伤害时调用） */
int record_combat_threat(object source, object target, int amount)
{
    mapping combat;
    mapping threat;
    string combat_id;

    if (!objectp(source) || !objectp(target) || amount <= 0)
        return 0;
    combat_id = query_combat_id(source);
    if (!stringp(combat_id))
        return 0;
    combat = _combats[combat_id];
    threat = mapp(combat) ? combat["threat"] : 0;
    if (!mapp(threat))
        return 0;
    return call_other(COMBAT_THREAT_HELPER, "record_combat_threat",
        threat, source, target, amount);
}

/* NPC 攻击目标选择：敌对侧仇恨最高者（无则 0，调用方回退原对手逻辑）。 */
object resolve_npc_attack_target(object actor)
{
    mapping combat;
    mapping sides;
    mapping threat;
    object *hostiles;
    string actor_side;
    string combat_id;

    if (!objectp(actor))
        return 0;
    combat_id = query_combat_id(actor);
    if (!stringp(combat_id))
        return 0;
    combat = _combats[combat_id];
    if (!mapp(combat))
        return 0;
    threat = mapp(combat["threat"]) ? combat["threat"] : ([ ]);
    sides = mapp(combat["sides"]) ? combat["sides"] : ([ ]);
    actor_side = stringp(sides[actor]) ? sides[actor]
        : (actor == combat["attacker"] ? "attacker" : "defender");

    hostiles = ({ });
    foreach (object participant in arrayp(combat["participants"])
        ? combat["participants"] : ({ })) {
        if (!objectp(participant) || participant == actor)
            continue;
        if ((stringp(sides[participant]) ? sides[participant]
            : (participant == combat["attacker"] ? "attacker" : "defender")) == actor_side)
            continue;
        hostiles += ({ participant });
    }

    return call_other(COMBAT_THREAT_HELPER, "resolve_threat_target",
        threat, actor, hostiles);
}

/* 多人战斗：玩家侧同袍依次行动（对仇恨目标普通攻击）。返回 1 表示有同袍行动过。 */
int run_ally_attacks(string combat_id, object player)
{
    mapping combat;
    mapping sides;
    object *participants;
    object ally;
    object target;
    string player_side;
    int acted;

    if (!stringp(combat_id) || !objectp(player))
        return 0;
    combat = _combats[combat_id];
    if (!mapp(combat))
        return 0;
    participants = arrayp(combat["participants"]) ? combat["participants"] : ({ });
    sides = mapp(combat["sides"]) ? combat["sides"] : ([ ]);
    player_side = stringp(sides[player]) ? sides[player]
        : (player == combat["attacker"] ? "attacker" : "defender");

    acted = 0;
    foreach (ally in participants) {
        if (!objectp(ally) || ally == player || ally == combat["attacker"]
            || ally == combat["defender"])
            continue;
        if ((stringp(sides[ally]) ? sides[ally]
            : (ally == combat["attacker"] ? "attacker" : "defender")) != player_side)
            continue;
        if ((ally->query("combat/hp") || 0) <= 0)
            continue;
        target = resolve_npc_attack_target(ally);
        if (!objectp(target) || (target->query("combat/hp") || 0) <= 0)
            continue;
        call_other(COMBAT_ROUNDS_HELPER, "run_single_attack",
            this_object(), ally, target, !combat["kill_mode"]);
        acted = 1;
    }
    return acted;
}

/* 多人战斗：玩家侧进攻后，敌方侧是否已全灭（用于结束战斗） */
int query_enemy_side_down(string combat_id, object player)
{
    mapping combat;
    mapping sides;
    object *participants;
    object participant;
    string player_side;

    if (!stringp(combat_id) || !objectp(player))
        return 1;
    combat = _combats[combat_id];
    if (!mapp(combat))
        return 1;
    participants = arrayp(combat["participants"]) ? combat["participants"] : ({ });
    sides = mapp(combat["sides"]) ? combat["sides"] : ([ ]);
    player_side = stringp(sides[player]) ? sides[player]
        : (player == combat["attacker"] ? "attacker" : "defender");

    foreach (participant in participants) {
        if (!objectp(participant))
            continue;
        if ((stringp(sides[participant]) ? sides[participant]
            : (participant == combat["attacker"] ? "attacker" : "defender")) == player_side)
            continue;
        if ((participant->query("combat/hp") || 0) > 0)
            return 0;
    }
    return 1;
}

void stop_combat(object who)
{
    string combat_id;

    combat_id = query_combat_id(who);
    if (stringp(combat_id))
        stop_combat_by_id(combat_id);
}

mapping handle_player_logout(object player)
{
    if (objectp(player) && is_in_combat(player))
        stop_combat(player);
    return ([ "ok": 1, "state": "combat_released" ]);
}

void stop_combat_by_id(string combat_id)
{
    call_other(COMBAT_SESSION_HELPER, "stop_combat_by_id", _combats, _combat_id_map, combat_id);
}

void stop_fight(object who)
{
    stop_combat(who);
}

int attempt_flee(object who, string direction)
{
    mapping combat;
    object env;
    mapping exits;
    string combat_id;
    mapping flee_result;
    mapping travel_result;
    object opponent;
    int phase_started_at;
    int phase_cpu_started_at;

    if (!objectp(who))
        return 0;

    phase_started_at = perf_counter_ns();
    phase_cpu_started_at = thread_cpu_time_ns();
    combat_id = query_combat_id(who);
    combat = stringp(combat_id) ? _combats[combat_id] : 0;
    if (mapp(combat)) {
        if (combat["attacker"] == who)
            opponent = combat["defender"];
        else if (combat["defender"] == who)
            opponent = combat["attacker"];
    } else if (who->is_fighting()) {
        opponent = who->query_temp("fight_ob");
    }

    if (!is_in_combat(who) && who->is_fighting()) {
        clear_stale_fight_state(who);
        if (objectp(opponent) && !is_in_combat(opponent))
            clear_stale_fight_state(opponent);
    }

    if (!is_in_combat(who) && !who->is_fighting()) {
        record_combat_phase(who, "flee_precheck", phase_started_at, phase_cpu_started_at);
        combat_msg(who, "你现在没有在战斗中。\n");
        return 1;
    }

    env = environment(who);
    if (!objectp(env)) {
        combat_msg(who, "你不知道在哪里。\n");
        return 1;
    }

    exits = env->query_exits();
    record_combat_phase(who, "flee_precheck", phase_started_at, phase_cpu_started_at);
    phase_started_at = perf_counter_ns();
    phase_cpu_started_at = thread_cpu_time_ns();
    flee_result = call_other(COMBAT_FLEE_HELPER, "resolve_flee_attempt", who, env, exits, direction);
    record_combat_phase(who, "flee_resolve", phase_started_at, phase_cpu_started_at);
    if (!mapp(flee_result))
        return 1;

    if (flee_result["status"] == "invalid") {
        combat_msg(who, flee_result["message"] || "\n");
        return 1;
    }

    if (flee_result["status"] == "failed") {
        combat_msg(who, flee_result["message"] || "\n");
        who->add_busy(flee_result["failure_busy_seconds"] || 2);
        return 1;
    }

    combat_msg(who, flee_result["message"] || "\n");
    phase_started_at = perf_counter_ns();
    phase_cpu_started_at = thread_cpu_time_ns();
    travel_result = WORLD_TRAVEL_D->move_actor(who,
        flee_result["destination"], ([
            "reason": "combat_flee",
            "direction": flee_result["direction"],
            "expected_location_generation":
                who->query_temp("world/location_generation") || 0,
        ]));
    if (!mapp(travel_result) || !travel_result["ok"]) {
        record_combat_phase(who, "flee_move", phase_started_at, phase_cpu_started_at);
        combat_msg(who, HIR "你一时慌乱，没能成功逃离当前战斗！" NOR "\n");
        return 1;
    }
    record_combat_phase(who, "flee_move", phase_started_at, phase_cpu_started_at);
    if (function_exists("interrupt_busy", who))
        who->interrupt_busy();

    phase_started_at = perf_counter_ns();
    phase_cpu_started_at = thread_cpu_time_ns();
    send_flee_end_packets(who, opponent);
    if (mapp(combat))
        stop_fight(who);
    else {
        clear_stale_fight_state(who);
        clear_stale_fight_state(opponent);
    }
    NUMERIC_RULE_D->mark_combat_reengage_cooldown(who, ([
        "result": "flee",
        "kill_mode": mapp(combat) && combat["kill_mode"] ? 1 : 0,
    ]));
    record_combat_phase(who, "flee_finalize", phase_started_at, phase_cpu_started_at);
    phase_started_at = perf_counter_ns();
    phase_cpu_started_at = thread_cpu_time_ns();
    call_other("/lib/std/utils/util_view", "refresh_room_view", who, environment(who));
    record_combat_phase(who, "flee_refresh", phase_started_at, phase_cpu_started_at);
    return 1;
}
