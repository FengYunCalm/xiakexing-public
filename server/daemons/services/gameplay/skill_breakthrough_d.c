/**
 * @file    skill_breakthrough_d.c
 * @brief   核心公开武学、开放层级和突破选择的唯一真源。
 *
 * 数值上限、突破间隔和长期层数始终由 NUMERIC_RULE_D 决定；本服务只表达
 * 哪些手工武学已经正式开放、当前开放到哪一层，以及独立的选择存档。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

#define LEGACY_TRANSITION_CHOICE_PREFIX "beta0_v2_"
#define CURRENT_CHOICE_PREFIX "breakthrough_"

string query_choice(object player, string skill_id, int tier);
private int query_has_selection_history(object player, string skill_id, int tier);

private void invalidate_skill_protocol_cache(object player, string skill_id)
{
    if (!objectp(player))
        return;
    SKILL_D->invalidate_player_skill_projection(player, skill_id);
}

string *query_public_skill_ids()
{
    return NUMERIC_RULE_D->query_public_skill_ids();
}

int is_public_skill(string skill_id)
{
    return NUMERIC_RULE_D->is_public_skill(skill_id);
}

int query_public_interval()
{
    return NUMERIC_RULE_D->query_skill_breakthrough_interval();
}

int query_public_max_tier()
{
    return NUMERIC_RULE_D->query_skill_breakthrough_max_tier();
}

int query_public_skill_cap()
{
    return NUMERIC_RULE_D->query_public_skill_cap();
}

int query_release_max_tier()
{
    return NUMERIC_RULE_D->query_skill_breakthrough_release_max_tier();
}

private string active_choice_path(string skill_id, int tier)
{
    return sprintf("skill_breakthrough/active_choices/%s/%d", skill_id, tier);
}

private string legacy_choice_path(string skill_id, int tier)
{
    return sprintf("skill_breakthrough/choices/%s/%d", skill_id, tier);
}

private string selection_history_path(string skill_id, int tier)
{
    return sprintf("skill_breakthrough/selection_history/%s/%d", skill_id, tier);
}

/* Read-only migration paths for saves written before active_choices existed. */
private string legacy_transition_choice_path(string skill_id, int tier)
{
    return sprintf("skill_breakthrough/choices_v2/%s/%d", skill_id, tier);
}

/* 突破选择与选择历史是玩家持久状态：经玩家状态 owner 原子提交
 * （set + delete 同批次），失败返回 0，调用方不得伪报已写入。 */
private int commit_breakthrough_state(object player, mapping changes,
    string *deletes, string operation)
{
    mapping mutation;
    mapping response;

    if (!objectp(player) || !mapp(changes) || !arrayp(deletes)
        || (sizeof(changes) == 0 && sizeof(deletes) == 0))
        return 0;
    mutation = ([ "changes": changes, "deletes": deletes ]);
    response = PLAYER_STATE_D->commit_state_mutations(player, changes, deletes,
        PLAYER_STATE_D->build_state_idempotency_key(player,
            operation || "skill/breakthrough_state", mutation));
    return mapp(response) && response["ok"];
}

private string legacy_transition_history_path(string skill_id, int tier)
{
    return sprintf("skill_breakthrough/history_v2/%s/%d", skill_id, tier);
}

private int query_has_selection_history(object player, string skill_id, int tier)
{
    mixed history;

    if (!objectp(player) || !stringp(skill_id) || skill_id == "" || tier <= 0)
        return 0;
    history = player->query(selection_history_path(skill_id, tier));
    if (!arrayp(history) || sizeof(history) == 0)
        history = player->query(legacy_transition_history_path(skill_id, tier));
    return arrayp(history) && sizeof(history) > 0;
}

private string normalize_choice_id(mixed choice)
{
    if (!stringp(choice) || choice == "")
        return "";
    if (strsrch(choice, LEGACY_TRANSITION_CHOICE_PREFIX) == 0)
        return CURRENT_CHOICE_PREFIX
            + choice[strlen(LEGACY_TRANSITION_CHOICE_PREFIX)..];
    return choice;
}

private mapping *query_raw_candidates(string skill_id, int tier)
{
    if (!is_public_skill(skill_id) || tier <= 0 || tier > query_public_max_tier())
        return ({ });
    return NUMERIC_RULE_D->query_skill_breakthrough_candidates(skill_id, tier);
}

mapping *query_public_candidates(string skill_id, int tier)
{
    if (!is_public_skill(skill_id) || tier <= 0 || tier > query_release_max_tier())
        return ({ });
    return query_raw_candidates(skill_id, tier);
}

private mapping find_candidate(string skill_id, int tier, string entry_id)
{
    mapping entry;

    if (!stringp(entry_id) || entry_id == "")
        return 0;
    foreach (entry in query_raw_candidates(skill_id, tier))
        if (mapp(entry) && entry["id"] == entry_id)
            return entry;
    return 0;
}

int query_unlocked_tier(object player, string skill_id)
{
    int level;
    int tier;

    if (!objectp(player) || !is_public_skill(skill_id))
        return 0;
    level = NUMERIC_RULE_D->query_effective_skill_level(player, skill_id);
    tier = NUMERIC_RULE_D->query_skill_breakthrough_tier_by_level(level);
    if (tier > query_release_max_tier())
        tier = query_release_max_tier();
    return tier < 0 ? 0 : tier;
}

string query_choice(object player, string skill_id, int tier)
{
    mixed choice;

    if (!objectp(player) || !is_public_skill(skill_id) || tier <= 0)
        return "";
    choice = player->query(active_choice_path(skill_id, tier));
    if (!stringp(choice) || choice == "")
        choice = player->query(legacy_transition_choice_path(skill_id, tier));
    return normalize_choice_id(choice);
}

private string query_legacy_choice(object player, string skill_id, int tier)
{
    mixed choice;

    if (!objectp(player) || !stringp(skill_id) || skill_id == "" || tier <= 0)
        return "";
    choice = player->query(legacy_choice_path(skill_id, tier));
    return stringp(choice) ? choice : "";
}

mapping query_legacy_dormant_choices(object player, string skill_id)
{
    mapping result;
    string entry_id;
    int tier;

    result = ([ ]);
    if (!objectp(player) || !stringp(skill_id) || skill_id == "")
        return result;
    for (tier = 1; tier <= query_public_max_tier(); tier++) {
        entry_id = query_legacy_choice(player, skill_id, tier);
        if (entry_id != "")
            result[tier] = ([ "entry_id": entry_id, "legacy_dormant": 1 ]);
    }
    return result;
}

mapping query_choices(object player, string skill_id)
{
    mapping result;
    string entry_id;
    int tier;

    result = ([ ]);
    if (!objectp(player) || !is_public_skill(skill_id))
        return result;
    for (tier = 1; tier <= query_release_max_tier(); tier++) {
        entry_id = query_choice(player, skill_id, tier);
        if (entry_id != "")
            result[tier] = entry_id;
    }
    return result;
}

mapping *query_selected_entries(object player, string skill_id)
{
    mapping *entries;
    mapping entry;
    string entry_id;
    int tier;
    int unlocked_tier;

    entries = ({ });
    if (!objectp(player) || !is_public_skill(skill_id))
        return entries;
    unlocked_tier = query_unlocked_tier(player, skill_id);
    for (tier = 1; tier <= unlocked_tier; tier++) {
        entry_id = query_choice(player, skill_id, tier);
        if (entry_id == "")
            continue;
        entry = find_candidate(skill_id, tier, entry_id);
        if (mapp(entry))
            entries += ({ entry });
    }
    return entries;
}

mapping *query_available_choices(object player, string skill_id)
{
    mapping *available;
    mapping *candidates;
    int tier;
    int unlocked_tier;

    available = ({ });
    if (!objectp(player) || !is_public_skill(skill_id))
        return available;
    unlocked_tier = query_unlocked_tier(player, skill_id);
    /* 只发布当前第一个未完成节点；后续节点必须按顺序完成。 */
    for (tier = 1; tier <= unlocked_tier; tier++) {
        if (query_choice(player, skill_id, tier) != "")
            continue;
        candidates = query_public_candidates(skill_id, tier);
        if (sizeof(candidates) > 0) {
            available += ({ ([
                "tier": tier,
                "level": NUMERIC_RULE_D->query_skill_breakthrough_level_by_tier(tier),
                "candidates": candidates,
            ]) });
        }
        break;
    }
    return available;
}

int query_next_level(object player, string skill_id)
{
    int level;
    int next_level;
    int tier;

    if (!objectp(player) || !is_public_skill(skill_id))
        return 0;
    level = NUMERIC_RULE_D->query_effective_skill_level(player, skill_id);
    /* 找第一个未选择的突破节点，返回其等级；全选完返回 0。 */
    for (tier = 1; tier <= query_release_max_tier(); tier++) {
        next_level = NUMERIC_RULE_D->query_skill_breakthrough_level_by_tier(tier);
        if (next_level <= 0)
            break;
        if (level < next_level)
            return next_level;
        if (query_choice(player, skill_id, tier) == "")
            return next_level;
    }
    return 0;
}

mapping can_choose(object player, string skill_id, int tier, string entry_id)
{
    mapping entry;
    int unlocked_tier;
    int previous_tier;

    if (!objectp(player) || !is_public_skill(skill_id) || tier <= 0
        || tier > query_release_max_tier() || !stringp(entry_id) || entry_id == "")
        return ([ "ok": 0, "error": "skill_not_public" ]);
    if (query_choice(player, skill_id, tier) != "")
        return ([ "ok": 0, "error": "already_chosen" ]);
    unlocked_tier = query_unlocked_tier(player, skill_id);
    if (tier > unlocked_tier)
        return ([ "ok": 0, "error": "tier_not_unlocked", "unlocked_tier": unlocked_tier ]);
    for (previous_tier = 1; previous_tier < tier; previous_tier++) {
        if (query_choice(player, skill_id, previous_tier) == "")
            return ([ "ok": 0, "error": "previous_tier_required",
                "required_tier": previous_tier,
                "unlocked_tier": unlocked_tier ]);
    }
    entry = find_candidate(skill_id, tier, entry_id);
    if (!mapp(entry))
        return ([ "ok": 0, "error": "missing_candidate" ]);
    return ([ "ok": 1, "entry": entry, "unlocked_tier": unlocked_tier ]);
}

mapping query_upgrade_gate(object player, string skill_id)
{
    int level;
    int tier;
    int node_level;

    if (!objectp(player) || !is_public_skill(skill_id))
        return ([ "ok": 1, "blocked": 0 ]);
    level = NUMERIC_RULE_D->query_effective_skill_level(player, skill_id);
    for (tier = 1; tier <= query_release_max_tier(); tier++) {
        node_level = NUMERIC_RULE_D->query_skill_breakthrough_level_by_tier(tier);
        if (node_level <= 0)
            break;
        if (query_choice(player, skill_id, tier) == "")
            return ([
                "ok": 1,
                "blocked": level >= node_level ? 1 : 0,
                "skill_id": skill_id,
                "tier": tier,
                "required_level": node_level,
                "available": query_available_choices(player, skill_id),
            ]);
    }
    return ([ "ok": 1, "blocked": 0, "skill_id": skill_id,
        "required_level": 0 ]);
}

mapping choose_breakthrough(object player, string skill_id, int tier, string entry_id)
{
    mapping entry;
    mapping result;
    mapping changes;
    int first_tier_selection;

    result = can_choose(player, skill_id, tier, entry_id);
    if (!mapp(result) || !result["ok"])
        return result;
    entry = result["entry"];
    first_tier_selection = tier == 1
        && !query_has_selection_history(player, skill_id, 1);
    changes = ([
        active_choice_path(skill_id, tier): entry_id,
        selection_history_path(skill_id, tier): ({ ([
            "entry_id": entry_id,
            "chosen_at": time(),
            "chosen_level": SKILL_D->query_player_skill_level(player, skill_id),
            "snapshot": entry,
            "reason": "choose",
        ]) }),
    ]);
    if (!commit_breakthrough_state(player, changes, ({ }),
        "skill/breakthrough_choose"))
        return ([ "ok": 0, "error": "choice_write_failed" ]);
    if (query_choice(player, skill_id, tier) != entry_id)
        return ([ "ok": 0, "error": "choice_write_failed" ]);
    invalidate_skill_protocol_cache(player, skill_id);

    /* 首次突破只认该技能第一次完成 tier 1；重置后仍不重复计数。 */
    if (first_tier_selection) {
        QUEST_D->record_action_progress(player, "first_breakthrough_skill_ready",
            skill_id, "success");
        QUEST_D->record_action_progress(player, "first_breakthrough_selected",
            skill_id, "success");
    }
    return ([
        "ok": 1,
        "skill_id": skill_id,
        "tier": tier,
        "entry": entry,
        "selected_entries": query_selected_entries(player, skill_id),
        "available": query_available_choices(player, skill_id),
    ]);
}

private string *query_reset_teacher_providers(string skill_id)
{
    /* 动态反查活树教师配置（CONTENT_REGISTRY_D 单点），换代自动跟随；
     * 旧代硬编码 switch（guiyuan_xinfa/duanchao_blade/tingyu_sword 等）
     * 已随内容换代全部失效，突破重置对真实技能永远落空。
     * 返回全集：多师场景任一在场即可，单师场景集合恰含一个元素。 */
    return CONTENT_REGISTRY_D->query_skill_teacher_providers(skill_id);
}

private int player_is_with_reset_teacher(object player, string skill_id)
{
    object env;
    object ob;
    string *provider_ids;

    if (!objectp(player))
        return 0;
    provider_ids = query_reset_teacher_providers(skill_id);
    if (sizeof(provider_ids) == 0)
        return 0;
    env = environment(player);
    if (!objectp(env))
        return 0;
    foreach (ob in all_inventory(env))
        if (objectp(ob) && member_array(ob->query("provider_id"), provider_ids) != -1)
            return 1;
    return 0;
}

private int player_is_in_combat(object player)
{
    return objectp(player) && (COMBAT_D->is_in_combat(player)
        || objectp(player->query_temp("fight_ob")));
}

mapping can_reset_breakthrough(object player, string skill_id)
{
    if (!objectp(player) || !is_public_skill(skill_id))
        return ([ "ok": 0, "error": "skill_not_public" ]);
    if (query_choice(player, skill_id, 1) == "")
        return ([ "ok": 0, "error": "no_choice" ]);
    if (player_is_in_combat(player))
        return ([ "ok": 0, "error": "in_combat" ]);
    if (!player_is_with_reset_teacher(player, skill_id))
        return ([ "ok": 0, "error": "teacher_not_present" ]);
    return ([ "ok": 1, "skill_id": skill_id, "tier": 1 ]);
}

mapping reset_breakthrough(object player, string skill_id)
{
    mapping result;
    mixed history;
    string previous_choice;

    result = can_reset_breakthrough(player, skill_id);
    if (!mapp(result) || !result["ok"])
        return result;
    previous_choice = query_choice(player, skill_id, 1);
    history = player->query(selection_history_path(skill_id, 1));
    if (!arrayp(history))
        history = player->query(legacy_transition_history_path(skill_id, 1));
    if (!arrayp(history))
        history = ({ });
    history += ({ ([
        "entry_id": previous_choice,
        "reset_at": time(),
        "chosen_level": SKILL_D->query_player_skill_level(player, skill_id),
        "reason": "free_teacher_reset",
    ]) });
    if (!commit_breakthrough_state(player,
        ([ selection_history_path(skill_id, 1): history ]),
        ({ active_choice_path(skill_id, 1),
            legacy_transition_choice_path(skill_id, 1) }),
        "skill/breakthrough_reset"))
        return ([ "ok": 0, "error": "choice_reset_failed" ]);
    if (query_choice(player, skill_id, 1) != "")
        return ([ "ok": 0, "error": "choice_reset_failed" ]);
    invalidate_skill_protocol_cache(player, skill_id);
    return ([
        "ok": 1,
        "skill_id": skill_id,
        "tier": 1,
        "previous_choice": previous_choice,
        "available": query_available_choices(player, skill_id),
    ]);
}

mapping query_breakthrough_state(object player, string skill_id)
{
    mapping *available;
    mapping *selected;
    int unlocked_tier;
    int is_public;

    if (!objectp(player) || !stringp(skill_id) || skill_id == "")
        return ([ "ok": 0, "error": "invalid_arguments" ]);
    is_public = is_public_skill(skill_id);
    unlocked_tier = query_unlocked_tier(player, skill_id);
    selected = is_public ? query_selected_entries(player, skill_id) : ({ });
    available = is_public ? query_available_choices(player, skill_id) : ({ });
    return ([
        "ok": 1,
        "skill_id": skill_id,
        "kind": NUMERIC_RULE_D->query_skill_kind(skill_id),
        "level": NUMERIC_RULE_D->query_effective_skill_level(player, skill_id),
        "tier": unlocked_tier,
        "unlocked_tier": unlocked_tier,
        "interval": query_public_interval(),
        "max_tier": query_public_max_tier(),
        "release_max_tier": query_release_max_tier(),
        "next_level": is_public ? query_next_level(player, skill_id) : 0,
        "selected": selected,
        "entries": selected,
        "available": available,
        "pending_count": sizeof(available),
        "public": is_public,
        "legacy_dormant": query_legacy_dormant_choices(player, skill_id),
    ]);
}
