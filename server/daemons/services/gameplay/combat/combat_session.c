/**
 * @file    combat_session.c
 * @brief   战斗会话生命周期子服务



 */

#pragma strict_types

#include <system/globals.h>
#include <system/ansi.h>
#include <system/services.h>
#include <system/xk_protocol.h>
#include <game_constants.h>

#define COMBAT_MESSAGING_HELPER  "/daemons/services/gameplay/combat/combat_messaging"
#define COMBAT_OUTCOME_HELPER    "/daemons/services/gameplay/combat/combat_outcome"
#define COMBAT_STATE_HELPER      "/daemons/services/gameplay/combat/combat_state"
#define COMBAT_THREAT_HELPER     "/daemons/services/gameplay/combat/combat_threat"

void stop_combat_by_id(mapping combats, mapping combat_id_map, string combat_id);

private int query_combat_resource(object who, string key)
{
    int value;

    if (!objectp(who) || !stringp(key) || key == "")
        return -1;
    value = who->query("combat/" + key);
    return intp(value) ? value : -1;
}

private mapping build_opponent_vitals_payload(object opponent)
{
    return ([
        "opponent_hp": query_combat_resource(opponent, "hp"),
        "opponent_max_hp": query_combat_resource(opponent, "max_hp"),
        "opponent_mp": query_combat_resource(opponent, "mp"),
        "opponent_max_mp": query_combat_resource(opponent, "max_mp"),
        "opponent_ep": query_combat_resource(opponent, "ep"),
        "opponent_max_ep": query_combat_resource(opponent, "max_ep")
    ]);
}

void schedule_next_round(object combat_service, mapping combats, string combat_id)
{
    mapping combat;
    int timer_handle;
    int delay;

    combat = combats[combat_id];
    if (!mapp(combat))
        return;

    if (combat["timer"])
        TIMER_D->cancel_timer(combat["timer"]);

    combat["timer_generation"] = (combat["timer_generation"] || 0) + 1;
    delay = NUMERIC_RULE_D->query_combat_round_interval_seconds();
    combat["round_due_ns"] = perf_counter_ns() + delay * 1000000000;
    timer_handle = TIMER_D->call_later_walltime_background("process_round",
        delay, combat_service,
        combat_id, combat["timer_generation"]);
    combat["timer"] = timer_handle;
    combats[combat_id] = combat;
}

void start_combat_session(mapping combats, mapping combat_id_map,
    string combat_id, object attacker, object defender, int kill_mode)
{
    mapping combat;
    string attacker_name;
    string defender_name;

    attacker_name = attacker->query_name();
    defender_name = defender->query_name();
    combat = ([
        "id": combat_id,
        "attacker": attacker,
        "defender": defender,
        "attacker_name": attacker_name,
        "defender_name": defender_name,
        "defender_is_npc": function_exists("is_npc", defender) && call_other(defender, "is_npc"),
        "participants": ({ attacker, defender }),
        "sides": ([ attacker: "attacker", defender: "defender" ]),
        "threat": call_other(COMBAT_THREAT_HELPER, "init_combat_threat",
            attacker, defender),
        "state": COMBAT_STATE_FIGHTING,
        "round": 0,
        "timer": 0,
        "timer_generation": 0,
        "start_time": time(),
        "last_action": time(),
        "kill_mode": kill_mode ? 1 : 0
    ]);

    combats[combat_id] = combat;
    call_other(COMBAT_STATE_HELPER, "set_combat_id", combat_id_map, attacker, combat_id);
    call_other(COMBAT_STATE_HELPER, "set_combat_id", combat_id_map, defender, combat_id);
    call_other(COMBAT_STATE_HELPER, "sync_fight_state", attacker, defender);
    if (objectp(attacker))
        attacker->set_temp("combat/tempo/attack_progress_bp", 0);
    if (objectp(defender))
        defender->set_temp("combat/tempo/attack_progress_bp", 0);
    call_other(COMBAT_MESSAGING_HELPER, "combat_packet", attacker, XK_CBST,
        build_opponent_vitals_payload(defender) + ([
            "combat_id": combat_id,
            "opponent": defender_name,
            "kill_mode": kill_mode ? 1 : 0,
            "message": kill_mode
                ? sprintf("你向%s拔刃相向，战局骤然绷紧。", defender_name)
                : sprintf("你向%s拱手请教，切磋就此展开。", defender_name)
        ]));
    call_other(COMBAT_MESSAGING_HELPER, "combat_packet", defender, XK_CBST,
        build_opponent_vitals_payload(attacker) + ([
            "combat_id": combat_id,
            "opponent": attacker_name,
            "kill_mode": kill_mode ? 1 : 0,
            "message": kill_mode
                ? sprintf("%s已向你逼来，杀机陡起。", attacker_name)
                : sprintf("%s向你发起切磋，邀你过招。", attacker_name)
        ]));
    COMBAT_D->send_combat_runtime_snapshot(attacker);
    COMBAT_D->send_combat_runtime_snapshot(defender);
}

void upgrade_kill_mode(mapping combats, string combat_id)
{
    mapping combat;
    object attacker;
    object defender;
    string attacker_name;
    string defender_name;

    combat = combats[combat_id];
    if (!mapp(combat))
        return;

    if (combat["kill_mode"])
        return;

    combat["kill_mode"] = 1;
    combats[combat_id] = combat;

    attacker = combat["attacker"];
    defender = combat["defender"];
    attacker_name = objectp(attacker) ? (attacker->query_name() || attacker->query("name") || "对手") : "对手";
    defender_name = objectp(defender) ? (defender->query_name() || defender->query("name") || "对手") : "对手";

    if (objectp(attacker)) {
        call_other(COMBAT_MESSAGING_HELPER, "combat_packet", attacker, XK_CBST,
            build_opponent_vitals_payload(defender) + ([
                "combat_id": combat_id,
                "opponent": defender_name,
                "kill_mode": 1,
                "message": "你心念一沉，不再留手，这场较量已成生死战。",
                "result_label": "升级"
            ]));
    }

    if (objectp(defender)) {
        call_other(COMBAT_MESSAGING_HELPER, "combat_packet", defender, XK_CBST,
            build_opponent_vitals_payload(attacker) + ([
                "combat_id": combat_id,
                "opponent": attacker_name,
                "kill_mode": 1,
                "message": "对方杀意骤盛，这场较量已被逼成生死战。",
                "result_label": "升级"
            ]));
    }
}

private int participant_is_down(object who)
{
    if (!objectp(who))
        return 0;
    if ((who->query("combat/hp") || 0) <= 0)
        return 1;
    return who->query_temp("is_dying") ? 1 : 0;
}

private int apply_player_defeat(object loser, object winner, mapping combat);

/* L-17：结束/清理按既有的 sides/participants 构造统一集合，覆盖全部在战
 * 参与者（主角补全、去重、忽略已销毁对象），不再只处理 attacker/defender。 */
private object *query_end_combat_participants(mapping combat)
{
    object attacker;
    object defender;
    object participant;
    object *participants;
    object *resolved;

    resolved = ({ });
    if (!mapp(combat))
        return resolved;
    participants = arrayp(combat["participants"])
        ? combat["participants"] : ({ });
    foreach (participant in participants) {
        if (!objectp(participant) || member_array(participant, resolved) != -1)
            continue;
        resolved += ({ participant });
    }
    attacker = combat["attacker"];
    defender = combat["defender"];
    if (objectp(attacker) && member_array(attacker, resolved) == -1)
        resolved += ({ attacker });
    if (objectp(defender) && member_array(defender, resolved) == -1)
        resolved += ({ defender });
    return resolved;
}

private string query_end_participant_side(mapping combat, object participant)
{
    mapping sides;

    if (!mapp(combat) || !objectp(participant))
        return "";
    sides = mapp(combat["sides"]) ? combat["sides"] : 0;
    if (mapp(sides) && stringp(sides[participant]) && sides[participant] != "")
        return sides[participant];
    if (participant == combat["attacker"])
        return "attacker";
    if (participant == combat["defender"])
        return "defender";
    return "";
}

private string query_end_result_label(string result)
{
    if (result == COMBAT_RESULT_WIN)
        return "win";
    if (result == COMBAT_RESULT_LOSE)
        return "lose";
    return "draw";
}

void end_combat_session(mapping combats, mapping combat_id_map, string combat_id, string result)
{
    mapping combat;
    mapping outcome_budget;
    object attacker;
    object defender;
    object participant;
    object *participants;
    string defender_result;
    string side;
    string side_result;

    combat = combats[combat_id];
    if (!mapp(combat))
        return;

    attacker = combat["attacker"];
    defender = combat["defender"];
    participants = query_end_combat_participants(combat);
    defender_result = result;
    if (result == COMBAT_RESULT_WIN)
        defender_result = COMBAT_RESULT_LOSE;
    else if (result == COMBAT_RESULT_LOSE)
        defender_result = COMBAT_RESULT_WIN;

    outcome_budget = NUMERIC_RULE_D->query_combat_outcome_budget_report(([
        "result": result,
        "kill_mode": combat["kill_mode"] ? 1 : 0,
        "round_count": combat["round"] || 0,
        "spar_recovery": !combat["kill_mode"] ? 1 : 0,
    ]));

    /* L-17：切磋恢复逐个参与者判断，附加参与者同样恢复。 */
    if (!combat["kill_mode"]
        && (!mapp(outcome_budget) || outcome_budget["spar_recovery"])) {
        foreach (participant in participants) {
            if (!participant_is_down(participant))
                call_other(COMBAT_OUTCOME_HELPER, "recover_after_spar",
                    participant);
        }
    }

    /* 特化 NPC 胜负回调：声明 query_npc_combat_outcome 的 NPC 自行处理战败后果 */
    if (result == COMBAT_RESULT_WIN && objectp(attacker)
        && function_exists("query_npc_combat_outcome", attacker)
        && function_exists("is_npc", attacker)
        && call_other(attacker, "is_npc")) {
        call_other(attacker, "query_npc_combat_outcome", attacker, defender, result);
    } else if (result == COMBAT_RESULT_LOSE && objectp(defender)
        && function_exists("query_npc_combat_outcome", defender)
        && function_exists("is_npc", defender)
        && call_other(defender, "is_npc")) {
        call_other(defender, "query_npc_combat_outcome", defender, attacker, result);
    } else if (combat["kill_mode"]) {
        /* 通用死亡闭环仅用于生死战；切磋不得触发死亡扣费。 */
        if (result == COMBAT_RESULT_WIN && userp(defender) && objectp(attacker)
            && function_exists("is_npc", attacker) && call_other(attacker, "is_npc"))
            apply_player_defeat(defender, attacker, combat);
        else if (result == COMBAT_RESULT_LOSE && userp(attacker) && objectp(defender)
            && function_exists("is_npc", defender) && call_other(defender, "is_npc"))
            apply_player_defeat(attacker, defender, combat);
    }

    /* L-17：再战冷却按参与者侧别逐个登记，附加参与者同样受限。 */
    foreach (participant in participants) {
        side = query_end_participant_side(combat, participant);
        if (side == "attacker")
            side_result = result;
        else if (side == "defender")
            side_result = defender_result;
        else
            continue;
        NUMERIC_RULE_D->mark_combat_reengage_cooldown(participant, ([
            "result": side_result,
            "kill_mode": combat["kill_mode"] ? 1 : 0,
            "round_count": combat["round"] || 0,
        ]));
    }

    if (!mapp(outcome_budget) || outcome_budget["send_end_packet"]) {
        if (result == COMBAT_RESULT_WIN)
            call_other(COMBAT_OUTCOME_HELPER, "send_combat_win", attacker, defender,
                combat["attacker_name"], combat["defender_name"]);
        else if (result == COMBAT_RESULT_LOSE)
            call_other(COMBAT_OUTCOME_HELPER, "send_combat_win", defender, attacker,
                combat["defender_name"], combat["attacker_name"]);
        else if (result == COMBAT_RESULT_DRAW)
            call_other(COMBAT_OUTCOME_HELPER, "send_combat_draw", attacker, defender);
        /* L-17：附加参与者按自身侧别收到适用终态，结束通知不遗漏。 */
        foreach (participant in participants) {
            if (participant == attacker || participant == defender)
                continue;
            side = query_end_participant_side(combat, participant);
            if (side == "attacker")
                side_result = result;
            else if (side == "defender")
                side_result = defender_result;
            else
                continue;
            call_other(COMBAT_MESSAGING_HELPER, "combat_packet", participant,
                XK_CBED, ([
                    "result": query_end_result_label(side_result),
                    "opponent": side == "attacker"
                        ? (combat["defender_name"] || "")
                        : (combat["attacker_name"] || ""),
                    "message": "战斗已经结束。",
                    "result_label": side_result == COMBAT_RESULT_WIN ? "获胜"
                        : (side_result == COMBAT_RESULT_LOSE ? "落败" : "平局"),
                ]));
        }
    }

    stop_combat_by_id(combats, combat_id_map, combat_id);
}

void stop_combat_by_id(mapping combats, mapping combat_id_map, string combat_id)
{
    mapping combat;
    object attacker;
    object defender;
    object participant;
    object *participants;
    int training_dummy_recovered;

    combat = combats[combat_id];
    if (!mapp(combat))
        return;

    attacker = combat["attacker"];
    defender = combat["defender"];
    participants = query_end_combat_participants(combat);
    training_dummy_recovered = call_other(COMBAT_OUTCOME_HELPER,
        "recover_after_training_dummy", participants);

    if (combat["timer"])
        TIMER_D->cancel_timer(combat["timer"]);

    foreach (participant in participants) {
        if (!objectp(participant))
            continue;
        COMBAT_EFFECT_D->clear_combat_status_effects(participant);
        call_other(COMBAT_STATE_HELPER, "clear_combat_id", combat_id_map, participant);
        if (training_dummy_recovered && interactive(participant))
            xk_send_status_light(participant);
        /* L-17：pending 特技清理覆盖全部在战参与者。 */
        COMBAT_D->clear_pending_npc_combat_skill(participant);
    }
    call_other(COMBAT_STATE_HELPER, "clear_fight_state", attacker, defender);
    /* L-17：附加参与者的单对象 fight 状态同样清理。 */
    foreach (participant in participants) {
        if (participant == attacker || participant == defender)
            continue;
        call_other(COMBAT_STATE_HELPER, "clear_fight_state", participant, 0);
    }
    map_delete(combats, combat_id);
}

/* 多人战斗：新参与者加入既有战斗（ally 加入 player 所在侧）。
 * 仇恨：新参与者与敌对侧互记基础仇恨（combat_threat.seed_join_threat）。
 * 返回 1 表示加入成功。 */
int join_combat_participant(mapping combats, mapping combat_id_map,
    string combat_id, object participant, object ally)
{
    mapping combat;
    mapping threat;
    object *hostiles;
    object *participants;
    string side_anchor;

    combat = combats[combat_id];
    if (!mapp(combat) || !objectp(participant))
        return 0;
    participants = arrayp(combat["participants"])
        ? combat["participants"] : ({ });
    if (member_array(participant, participants) != -1)
        return 1;

    if (objectp(ally) && ally == combat["attacker"])
        side_anchor = "attacker";
    else if (objectp(ally) && ally == combat["defender"])
        side_anchor = "defender";
    else
        side_anchor = "defender";

    participants += ({ participant });
    combat["participants"] = participants;
    if (!mapp(combat["sides"]))
        combat["sides"] = ([
            combat["attacker"]: "attacker",
            combat["defender"]: "defender",
        ]);
    combat["sides"][participant] = side_anchor;

    /* 敌对侧 = 与 ally 不同侧的全部存活参与者 */
    hostiles = ({ });
    foreach (object other in participants) {
        if (!objectp(other) || other == participant)
            continue;
        if (side_anchor == "attacker" && other == combat["defender"])
            hostiles += ({ other });
        else if (side_anchor == "defender" && other == combat["attacker"])
            hostiles += ({ other });
    }

    threat = mapp(combat["threat"]) ? combat["threat"] : ([ ]);
    call_other(COMBAT_THREAT_HELPER, "seed_join_threat", threat, participant, hostiles);
    combat["threat"] = threat;
    combats[combat_id] = combat;

    call_other(COMBAT_STATE_HELPER, "set_combat_id", combat_id_map, participant, combat_id);
    call_other(COMBAT_STATE_HELPER, "sync_fight_state", participant,
        side_anchor == "attacker" ? combat["defender"] : combat["attacker"]);
    if (objectp(participant))
        participant->set_temp("combat/tempo/attack_progress_bp", 0);
    return 1;
}

/*
 * 通用死亡闭环：战败损失（经验 5% 不降级 + 铜钱 10% + 随机杂物丢失可拾取）、
 * 复活到安全点、虚弱状态（5 分钟属性 80%）。
 * 捕快押人等特化路径不走此分支（特化回调优先）。
 */
private int apply_player_defeat(object loser, object winner, mapping combat)
{
    object *items;
    object drop_item;
    object start_room;
    object drop_room;
    mapping state;
    mapping survival_budget;
    int exp;
    int exp_loss;
    int exp_loss_bp;
    int money_loss_bp;
    mapping death_receipt;
    int death_penalty_failed;
    string death_msg;
    string death_penalty_state;
    int i;

    if (!objectp(loser) || !userp(loser))
        return 0;
    drop_room = environment(loser);

    /* L-132：死亡损失消费 combat_survival_budget 配置真源（默认值已冻结为
     * 既有行为 500bp=5% / 1000bp=10%），诊断面板与执行绑定同一来源。 */
    survival_budget = NUMERIC_RULE_D->query_combat_survival_budget_report(loser, ([
        "event": "player_down",
        "kill_mode": combat["kill_mode"] ? 1 : 0,
    ]));
    exp_loss_bp = mapp(survival_budget) && intp(survival_budget["death_exp_loss_bp"])
        ? survival_budget["death_exp_loss_bp"] : 500;
    money_loss_bp = mapp(survival_budget) && intp(survival_budget["death_money_loss_bp"])
        ? survival_budget["death_money_loss_bp"] : 1000;

    /* 经验损失按 death_exp_loss_bp 计算（不降级）。 */
    exp = intp(loser->query("combat_exp")) ? loser->query("combat_exp") : 0;
    exp_loss = exp * exp_loss_bp / 10000;
    if (exp_loss > 0)
        loser->set("combat_exp", exp - exp_loss);

    /* 铜钱损失按 death_money_loss_bp 换算成整数百分比（默认 1000bp=10%，
     * 保留至少 1 文）——统一走 economy death settlement
     * （幂等记账 + 可审计 receipt），combat 不再直接持有扣钱规则。
     * 失败/unknown 由 settlement 内部记录到玩家持久 pending
     * （event-id mapping，不覆盖写）；combat 只消费 receipt 决定文案，
     * 仅在 receipt 明确 committed/duplicate 时宣称扣款。 */
    death_receipt = ECONOMY_SETTLEMENT_D->settle_death_penalty(loser, ([
        "event_id": sprintf("%s:%s", base_name(loser), combat["id"]),
        "rate": money_loss_bp / 100,
        "keep_min": 1,
    ]));
    if (mapp(death_receipt) && !death_receipt["ok"]) {
        /* 诚实文案——不得向玩家宣称已结算；pending 已持久化，
         * 运维经 economycheck deathpending 核对/恢复。 */
        death_penalty_state = death_receipt["state"] || "unknown";
        loser->set_temp("combat/death_penalty_last_error",
            sprintf("%s|%s", death_receipt["state"] || "unknown",
                death_receipt["last_error"] || ""));
        death_penalty_failed = 1;
    }

    /* 随机一件非绑定杂物丢失（留在原地，可拾取） */
    if (objectp(drop_room)) {
        items = all_inventory(loser);
        drop_item = 0;
        for (i = 0; i < sizeof(items); i++) {
            if (!objectp(items[i]) || items[i]->query("no_drop")
                || items[i]->query("equipped"))
                continue;
            drop_item = items[i];
            break;
        }
        if (objectp(drop_item) && drop_item->move(drop_room))
            msg_to_room(drop_room, loser->query("name") + "遗落了一件东西在地上。\n", ({ loser }));
    }

    /* 复活到安全点 + 虚弱 5 分钟 */
    start_room = find_object(CONFIG_D->query_start_room());
    if (!objectp(start_room))
        start_room = load_object(CONFIG_D->query_start_room());
    if (objectp(start_room))
        WORLD_TRAVEL_D->move_actor(loser, base_name(start_room), ([
            "reason": "player_defeat_revive",
            "quiet": 1,
        ]));
    state = loser->query("combat/weakened");
    if (!mapp(state))
        state = ([ ]);
    state["until"] = time() + 300;
    state["hp_scale"] = 80;
    loser->set("combat/weakened", state);
    loser->set("combat/hp", 1);
    loser->schedule_save();
    death_msg = death_penalty_failed
        ? (death_penalty_state == "unknown"
            ? "你损失了一些经验，但铜钱结算状态未知，请稍后查看。\n"
            : "你损失了一些经验，但铜钱结算异常（未扣除），请稍后查看。\n")
        : "你损失了一些经验与铜钱，一件随身杂物遗落在原地。\n";
    msg_send(loser, HIR "你眼前一黑，失去了知觉……\n" NOR
        "再醒来时，你已回到安全处。\n"
        HIY + death_msg
        + "战败的虚弱（5 分钟）让你状态下滑，可在药铺花钱调养解除。\n" NOR);
    return 1;
}

void abort_combat_session(mapping combats, mapping combat_id_map, string combat_id)
{
    mapping combat;
    object participant;
    object *participants;

    combat = combats[combat_id];
    if (!mapp(combat))
        return;
    participants = arrayp(combat["participants"])
        ? combat["participants"] : ({ combat["attacker"], combat["defender"] });
    stop_combat_by_id(combats, combat_id_map, combat_id);
    foreach (participant in participants) {
        if (!objectp(participant))
            continue;
        call_other(COMBAT_MESSAGING_HELPER, "combat_packet", participant, XK_CBED,
            ([
                "result": "interrupted",
                "message": "战斗因异常中断，请稍后再试。",
                "result_label": "中断",
            ]));
    }
}
