/**
 * @file    skill_unlock_d.c
 * @brief   技能解锁守护进程,查询玩家技能解锁状态。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

mapping query_unlock_state(object player, string unlock_type, string skill_id)
{
    int unlocked;

    if (!objectp(player) || !stringp(unlock_type) || unlock_type == "" || !stringp(skill_id) || skill_id == "")
        return ([ "ok": 0, "error": "invalid_arguments" ]);

    unlocked = player->query("skill_unlocks/" + unlock_type + "/" + skill_id) ? 1 : 0;
    return ([
        "ok": 1,
        "unlock_type": unlock_type,
        "skill_id": skill_id,
        "unlocked": unlocked,
    ]);
}

mapping can_unlock(object player, string unlock_type, string skill_id)
{
    mapping rule;
    mapping sect_status;
    int required_level;
    mixed allowed_sects;
    string sect_id;

    if (!objectp(player) || !stringp(unlock_type) || unlock_type == "" || !stringp(skill_id) || skill_id == "")
        return ([ "ok": 0, "error": "invalid_arguments" ]);

    rule = SKILL_UNLOCK_REGISTRY_D->query_unlock_rule(unlock_type, skill_id);
    if (!mapp(rule))
        return ([ "ok": 0, "error": "missing_unlock_rule" ]);

    required_level = rule["required_skill_level"] || 1;
    if (SKILL_D->query_player_skill_level(player, skill_id) < required_level)
        return ([ "ok": 0, "error": "skill_level_too_low" ]);

    allowed_sects = rule["allowed_sects"];
    if (arrayp(allowed_sects) && member_array("*", allowed_sects) == -1) {
        sect_status = SECT_D->query_player_sect_status(player);
        sect_id = mapp(sect_status) ? (sect_status["sect_id"] || "") : "";
        if (member_array(sect_id, allowed_sects) == -1)
            return ([ "ok": 0, "error": "sect_not_allowed" ]);
    }

    return ([ "ok": 1, "rule": rule ]);
}

mapping ensure_unlocked(object player, string unlock_type, string skill_id)
{
    mapping result;

    result = query_unlock_state(player, unlock_type, skill_id);
    if (!mapp(result) || !result["ok"])
        return result;
    if (unlock_type == "perform"
        && !mapp(SKILL_UNLOCK_REGISTRY_D->query_unlock_rule(unlock_type, skill_id)))
        return ([ "ok": 1, "just_unlocked": 0, "unlock_type": unlock_type, "skill_id": skill_id ]);
    if (result["unlocked"])
        return ([ "ok": 1, "just_unlocked": 0, "unlock_type": unlock_type, "skill_id": skill_id ]);

    result = can_unlock(player, unlock_type, skill_id);
    if (!mapp(result) || !result["ok"])
        return result;

    {
        mapping change;
        mapping commit;
        string path;

        path = "skill_unlocks/" + unlock_type + "/" + skill_id;
        change = ([ path: 1 ]);
        commit = PLAYER_STATE_D->commit_state_fields(player, change,
            PLAYER_STATE_D->build_state_idempotency_key(player,
                "skill/unlock", change));
        if (!mapp(commit) || !commit["ok"])
            return ([ "ok": 0, "error": "player_state_commit_failed" ]);
    }
    SKILL_D->invalidate_player_skill_projection(player, skill_id);
    return ([ "ok": 1, "just_unlocked": 1, "unlock_type": unlock_type, "skill_id": skill_id ]);
}
