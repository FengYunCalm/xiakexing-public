/**
 * @file    combat_outcome.c
 * @brief   战斗结束收尾子服务



 */

#pragma strict_types
#pragma save_types

#include <system/ansi.h>
#include <system/services.h>
#include <system/xk_protocol.h>

#define COMBAT_MESSAGING_HELPER "/daemons/services/gameplay/combat/combat_messaging"
#define BP_BASE 10000

void recover_after_spar(object who)
{
    object room;
    mapping policy;
    int max_hp;
    int max_mp;
    int recover_hp;
    int recover_hp_bp;
    int recover_mp;
    int recover_mp_bp;

    if (!objectp(who))
        return;

    policy = NUMERIC_RULE_D->query_combat_spar_recovery_policy();
    max_hp = who->query("combat/max_hp") || 100;
    max_mp = who->query("combat/max_mp") || 100;
    recover_hp_bp = mapp(policy) && !undefinedp(policy["spar_recover_hp_bp"])
        && intp(policy["spar_recover_hp_bp"]) ? policy["spar_recover_hp_bp"] : BP_BASE;
    recover_mp_bp = mapp(policy) && !undefinedp(policy["spar_recover_mp_bp"])
        && intp(policy["spar_recover_mp_bp"]) ? policy["spar_recover_mp_bp"] : BP_BASE;
    recover_hp = max_hp * recover_hp_bp / BP_BASE;
    recover_mp = max_mp * recover_mp_bp / BP_BASE;
    if (recover_hp_bp > 0 && recover_hp < 1)
        recover_hp = 1;
    if (recover_mp_bp > 0 && recover_mp < 1)
        recover_mp = 1;

    who->set_hp(recover_hp);
    who->set_mp(recover_mp);
    if (!mapp(policy) || policy["spar_clear_dying"]) {
        room = environment(who);
        who->delete_temp("is_dying");
        if (objectp(room))
            ROOM_VIEW_D->invalidate_room_view_content(room);
    }
}

int recover_after_training_dummy(object *participants)
{
    object participant;
    object room;
    int found_dummy;
    int max_hp;
    int max_mp;
    int max_ep;
    int changed;

    if (!arrayp(participants))
        return 0;
    foreach (participant in participants) {
        if (objectp(participant) && participant->query("training_dummy")) {
            found_dummy = 1;
            break;
        }
    }
    if (!found_dummy)
        return 0;

    foreach (participant in participants) {
        if (!objectp(participant))
            continue;
        max_hp = function_exists("query_max_hp", participant)
            ? participant->query_max_hp() : participant->query("combat/max_hp");
        max_mp = function_exists("query_max_mp", participant)
            ? participant->query_max_mp() : participant->query("combat/max_mp");
        max_ep = function_exists("query_max_ep", participant)
            ? participant->query_max_ep() : participant->query("combat/max_ep");
        if (max_hp > 0 && function_exists("set_hp", participant))
            changed += participant->set_hp(max_hp);
        if (max_mp > 0 && function_exists("set_mp", participant))
            changed += participant->set_mp(max_mp);
        if (max_ep > 0 && function_exists("set_ep", participant))
            changed += participant->set_ep(max_ep);
        participant->delete_temp("is_dying");
        room = environment(participant);
        if (objectp(room))
            ROOM_VIEW_D->invalidate_room_view_content(room);
    }
    return changed > 0 ? 1 : 0;
}

void send_combat_win(object winner, object loser, string winner_name, string loser_name)
{
    if (objectp(winner))
        winner_name = winner->query_name();
    if (objectp(loser))
        loser_name = loser->query_name();
    if (!stringp(winner_name) || winner_name == "")
        winner_name = "对手";
    if (!stringp(loser_name) || loser_name == "")
        loser_name = "对手";

    if (objectp(winner))
        call_other(COMBAT_MESSAGING_HELPER, "combat_packet", winner, XK_CBED,
            ([
                "result": "win",
                "opponent": loser_name,
                "message": sprintf("你一口气压住%s，终究拿下了这一战。", loser_name),
                "result_label": "获胜"
            ]));
    if (objectp(loser))
        call_other(COMBAT_MESSAGING_HELPER, "combat_packet", loser, XK_CBED,
            ([
                "result": "lose",
                "opponent": winner_name,
                "message": sprintf("你被%s压住了阵脚，这一战败下阵来。", winner_name),
                "result_label": "落败"
            ]));
}

void send_combat_draw(object attacker, object defender)
{
    if (objectp(attacker))
        call_other(COMBAT_MESSAGING_HELPER, "combat_packet", attacker, XK_CBED,
            ([
                "result": "draw",
                "opponent": objectp(defender) ? defender->query_name() : "",
                "message": sprintf("你与%s几番交错，终究谁也没能压过谁。",
                    objectp(defender) ? defender->query_name() : "对手"),
                "result_label": "平局"
            ]));
    if (objectp(defender))
        call_other(COMBAT_MESSAGING_HELPER, "combat_packet", defender, XK_CBED,
            ([
                "result": "draw",
                "opponent": objectp(attacker) ? attacker->query_name() : "",
                "message": sprintf("你与%s几番交错，终究谁也没能压过谁。",
                    objectp(attacker) ? attacker->query_name() : "对手"),
                "result_label": "平局"
            ]));
}
