/**
 * @file    combat_state.c
 * @brief   战斗状态维护子服务



 */

#pragma strict_types

#include <system/services.h>

private void clear_single_fight_state(object who, object opponent)
{
    if (!objectp(who))
        return;

    if (objectp(opponent) && function_exists("remove_enemy", who))
        who->remove_enemy(opponent);
    who->delete_temp("fight_ob");
    who->delete_temp("enemies");
    who->delete_temp("combat/tempo/attack_progress_bp");
}

void set_combat_id(mapping combat_id_map, object who, string combat_id)
{
    if (objectp(who))
        combat_id_map[who] = combat_id;
}

void clear_combat_id(mapping combat_id_map, object who)
{
    if (objectp(who))
        map_delete(combat_id_map, who);
}

void sync_fight_state(object attacker, object defender)
{
    if (objectp(attacker) && function_exists("set_fight", attacker))
        attacker->set_fight(defender);

    if (objectp(defender) && function_exists("set_fight", defender))
        defender->set_fight(attacker);
}

void clear_fight_state(object attacker, object defender)
{
    clear_single_fight_state(attacker, defender);
    clear_single_fight_state(defender, attacker);
}

string resolve_active_combat_id(object combat_service, mapping combats,
    mapping combat_id_map, object who)
{
    string combat_id;
    mapping combat;

    if (!objectp(who))
        return 0;

    combat_id = combat_id_map[who];
    if (!stringp(combat_id) || combat_id == "")
        return 0;

    combat = combats[combat_id];
    if (!mapp(combat)
        || (combat["attacker"] != who && combat["defender"] != who)) {
        clear_combat_id(combat_id_map, who);
        combat_service->clear_stale_fight_state(who);
        return 0;
    }

    return combat_id;
}

private mapping query_cleanup_budget(mapping combats, mapping combat, string reason)
{
    int now;

    now = time();
    return NUMERIC_RULE_D->query_combat_cleanup_budget_report(([
        "reason": reason,
        "age_seconds": mapp(combat) ? now - (combat["start_time"] || now) : 0,
        "idle_seconds": mapp(combat) ? now - (combat["last_action"] || now) : 0,
        "active_combats": mapp(combats) ? sizeof(combats) : 0,
    ]));
}

private int cleanup_budget_allows(mapping combats, mapping combat, string reason)
{
    mapping budget;

    budget = query_cleanup_budget(combats, combat, reason);
    return (!mapp(budget) || (budget["cleanup_allowed"] && !budget["block_apply"])) ? 1 : 0;
}

void purge_stale_combats(object combat_service, mapping combats, mapping combat_id_map)
{
    string *combat_ids;
    string combat_id;
    mapping combat;
    object attacker;
    object defender;

    combat_ids = keys(copy(combats));
    foreach (combat_id in combat_ids) {
        combat = combats[combat_id];
        if (!mapp(combat)) {
            map_delete(combats, combat_id);
            continue;
        }

        attacker = combat["attacker"];
        defender = combat["defender"];
        if (!objectp(attacker) || !objectp(defender)) {
            if (cleanup_budget_allows(combats, combat, "invalid_object"))
                combat_service->stop_combat_by_id(combat_id);
            continue;
        }

        if (combat_id_map[attacker] != combat_id || combat_id_map[defender] != combat_id) {
            if (cleanup_budget_allows(combats, combat, "id_mismatch"))
                combat_service->stop_combat_by_id(combat_id);
            continue;
        }

        if (!objectp(attacker->query_temp("fight_ob"))
            && !objectp(defender->query_temp("fight_ob"))) {
            if (cleanup_budget_allows(combats, combat, "no_fight_state"))
                combat_service->stop_combat_by_id(combat_id);
        }
    }
}

mapping query_all_combats(object combat_service, mapping combats, mapping combat_id_map)
{
    purge_stale_combats(combat_service, combats, combat_id_map);
    return copy(combats);
}

int is_in_combat(object combat_service, mapping combats, mapping combat_id_map, object who)
{
    return stringp(resolve_active_combat_id(combat_service, combats, combat_id_map, who));
}

string query_combat_id(object combat_service, mapping combats, mapping combat_id_map, object who)
{
    return resolve_active_combat_id(combat_service, combats, combat_id_map, who);
}
