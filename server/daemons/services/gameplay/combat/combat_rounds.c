/**
 * @file    combat_rounds.c
 * @brief   战斗回合编排子服务



 */

#pragma strict_types

#include <system/services.h>
#include <system/xk_protocol.h>
#include <game_constants.h>

private int actor_can_attack(object actor)
{
    if (!objectp(actor) || actor->query("no_attack"))
        return 0;
    if (function_exists("is_busy", actor) && actor->is_busy())
        return 0;
    return 1;
}

private int run_single_attack(object combat_service, object attacker, object defender,
    int nonlethal)
{
    int attack_result;

    if (!objectp(combat_service) || !objectp(attacker) || !objectp(defender))
        return 0;
    if ((defender->query("combat/hp") || 0) <= 0)
        return 1;

    attack_result = combat_service->do_single_attack(attacker, defender, nonlethal);
    if (attack_result)
        return 1;
    if (!objectp(defender))
        error("Combat target disappeared without a defeat result.\n");
    return (defender->query("combat/hp") || 0) <= 0;
}

private int should_finalize_status_defeat(object actor, mapping status)
{
    if (mapp(status) && status["defeated"])
        return 1;
    return objectp(actor) && (actor->query("combat/hp") || 0) <= 0;
}

private int process_actor_status(object combat_service, string combat_id, object actor,
    object opponent, string defeat_result, int nonlethal)
{
    mapping status;

    status = COMBAT_EFFECT_D->process_combat_status_effects(actor, opponent, nonlethal);
    if (should_finalize_status_defeat(actor, status)) {
        if (!nonlethal && objectp(actor))
            combat_service->handle_npc_defeat_effects(actor, opponent);
        combat_service->end_combat(combat_id, defeat_result);
        return 2;
    }
    if (!objectp(actor) || !objectp(opponent)) {
        combat_service->end_combat(combat_id, COMBAT_RESULT_DRAW);
        return 2;
    }
    if (mapp(status) && status["action_blocked"]) {
        combat_service->combat_packet(actor, XK_CBRD, ([
            "event": "status_block",
            "opponent": opponent->query_name(),
            "target": actor->query_name(),
            "message": "一股滞涩劲力缠住了你的身形，没能出手。",
            "result_label": "受制",
            "statuses": ({ ([
                "id": "status_block",
                "name": "受制",
                "category": "control",
                "type": "control",
                "owner": "self",
                "remaining_seconds": 1,
                "remaining_rounds": 1,
                "stacks": 1,
                "visible": 1,
            ]) }),
        ]));
        return 1;
    }
    return 0;
}

private void apply_round_resource_budget(object actor)
{
    if (objectp(actor))
        NUMERIC_RULE_D->apply_combat_resource_budget(actor, ([ "action_type": "round_start" ]));
}

int run_round(object combat_service, string combat_id, object attacker, object defender,
    int round_no, int nonlethal)
{
    object actor;
    object attack_target;
    object opponent;
    object threat_target;
    string actor_defeat_result;
    string opponent_defeat_result;
    int blocked;
    int skill_occupied;

    if (!objectp(combat_service) || !stringp(combat_id) || !objectp(attacker) || !objectp(defender))
        return 0;

    if (round_no % 2 == 1) {
        actor = attacker;
        opponent = defender;
        actor_defeat_result = COMBAT_RESULT_LOSE;
        opponent_defeat_result = COMBAT_RESULT_WIN;
    } else {
        actor = defender;
        opponent = attacker;
        actor_defeat_result = COMBAT_RESULT_WIN;
        opponent_defeat_result = COMBAT_RESULT_LOSE;
    }

    apply_round_resource_budget(actor);

    /* 多人战斗：NPC 回合按仇恨值选择攻击目标（1v1 时唯一敌对目标=原对手，行为不变） */
    if (!userp(actor)) {
        threat_target = combat_service->resolve_npc_attack_target(actor);
        if (objectp(threat_target))
            opponent = threat_target;
    }
    blocked = process_actor_status(combat_service, combat_id, actor, opponent, actor_defeat_result, nonlethal);
    if (blocked == 2 || !objectp(actor) || (actor->query("combat/hp") || 0) <= 0)
        return 1;
    if (blocked)
        combat_service->clear_pending_npc_combat_skill(actor);
    if (!blocked) {
        if (!actor_can_attack(actor)) {
            combat_service->clear_pending_npc_combat_skill(actor);
        } else {
            skill_occupied = combat_service->maybe_trigger_npc_combat_skill(
                actor, opponent, nonlethal);
            if (!objectp(actor) || !objectp(opponent)) {
                combat_service->end_combat(combat_id, COMBAT_RESULT_DRAW);
                return 1;
            }
            if ((opponent->query("combat/hp") || 0) <= 0) {
                combat_service->end_combat(combat_id, opponent_defeat_result);
                return 1;
            }
            attack_target = opponent;
            if (!skill_occupied
                && run_single_attack(combat_service, actor, attack_target, nonlethal)
                && attack_target == opponent) {
                combat_service->end_combat(combat_id, opponent_defeat_result);
                return 1;
            }
            /* 多人战斗：玩家行动后同侧同袍依次行动（目标由仇恨决定） */
            if (userp(actor)
                && combat_service->run_ally_attacks(combat_id, actor)
                && (combat_service->query_enemy_side_down(combat_id, actor))) {
                combat_service->end_combat(combat_id, opponent_defeat_result);
                return 1;
            }
        }
    }

    combat_service->schedule_next_round(combat_id);
    return 1;
}
