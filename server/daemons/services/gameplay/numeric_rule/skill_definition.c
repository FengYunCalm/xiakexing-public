/**
 * @file    numeric_rule/skill_definition.c
 * @brief   技能定义与训练上限
 *
 * @par Description:
 * 含 {query_skill_definition, query_skill_kind, query_skill_category, query_skill_potential_factor_bp, normalize_requirement_bp, query_basic_requirement_levels, query_basic_requirement_cap, skill_training_cap_cache_matches, query_skill_training_cap, query_effective_skill_level, query_skill_breakthrough_interval, query_skill_breakthrough_level_by_tier, is_public_skill, query_public_skill_cap, query_public_skill_ids, query_skill_breakthrough_max_tier, query_skill_breakthrough_release_max_tier, query_skill_breakthrough_tier_by_level, query_skill_breakthrough_tier}。
 *
 * 本模块是 skill_definition 域纯规则片段，由 `numeric_rule_d.c` 继承；
 * 不持有 daemon 状态（nosave 全部留在 facade），
 * 配置经 facade 的 query_section/query_section_int 读取。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <game/skill.h>
#include <game_constants.h>
#include "defs.h"
int query_max_skill_level();
int query_numeric_config_generation();
mapping query_section(string section_name);
int query_section_int(string section_name, string key, int default_value);
int *query_skill_breakthrough_levels();
int query_skill_cap_by_exp(int combat_exp);
mapping query_skill_numeric_definition(string skill_id);
mapping query_skill_numeric_metadata(string skill_id);
int is_public_skill(string skill_id);
int query_public_skill_cap();
int query_skill_breakthrough_interval();
int query_skill_breakthrough_max_tier();
string query_skill_category(string skill_id);
mapping query_skill_definition(string skill_id);
mapping query_skill_definition(string skill_id)
{
    mapping def;

    if (!stringp(skill_id) || skill_id == "")
        return 0;

    def = query_skill_numeric_definition(skill_id);
    return mapp(def) ? copy(def) : 0;
}
string query_skill_kind(string skill_id)
{
    mapping metadata;

    metadata = query_skill_numeric_metadata(skill_id);
    return mapp(metadata) && stringp(metadata["kind"])
        && metadata["kind"] != "" ? metadata["kind"] : "special";
}
string query_skill_category(string skill_id)
{
    mapping metadata;

    metadata = query_skill_numeric_metadata(skill_id);
    return mapp(metadata) && stringp(metadata["category"])
        ? metadata["category"] : "";
}
int query_skill_potential_factor_bp(string skill_id)
{
    mapping def;
    mapping defaults;
    mapping by_kind;
    int factor;

    def = query_skill_numeric_definition(skill_id);
    if (mapp(def) && intp(def["potential_factor_bp"]) && def["potential_factor_bp"] > 0)
        return def["potential_factor_bp"];

    defaults = query_section("skill_kind_defaults");
    by_kind = mapp(defaults) ? defaults[query_skill_kind(skill_id)] : 0;
    factor = mapp(by_kind) && intp(by_kind["potential_factor_bp"]) ? by_kind["potential_factor_bp"] : BP_BASE;
    return factor > 0 ? factor : BP_BASE;
}
private int normalize_requirement_bp(mixed raw_value)
{
    int value;

    if (!intp(raw_value))
        return BP_BASE;
    value = raw_value;
    if (value <= 0)
        return BP_BASE;
    if (value <= 100)
        return value * 100;
    return value;
}
private mapping query_basic_requirement_levels(object player, mapping requirements)
{
    string basic_skill;
    mapping levels;

    levels = ([ ]);
    if (!objectp(player) || !mapp(requirements))
        return levels;
    foreach (basic_skill in keys(requirements))
        if (stringp(basic_skill) && basic_skill != "")
            levels[basic_skill] = SKILL_D->query_player_skill_level(
                player, basic_skill);
    return levels;
}
private int query_basic_requirement_cap(mapping requirements,
    mapping requirement_levels)
{
    string basic_skill;
    mixed raw_ratio;
    int basic_level;
    int cap;
    int ratio_bp;

    cap = query_max_skill_level();
    foreach (basic_skill, raw_ratio in requirements) {
        if (!stringp(basic_skill) || basic_skill == "")
            continue;
        ratio_bp = normalize_requirement_bp(raw_ratio);
        basic_level = requirement_levels[basic_skill] || 0;
        if (ratio_bp > 0 && basic_level * BP_BASE / ratio_bp < cap)
            cap = basic_level * BP_BASE / ratio_bp;
    }

    return cap;
}
private int skill_training_cap_cache_matches(object player, mapping cache,
    int combat_exp)
{
    mapping requirement_levels;
    string basic_skill;

    if (!mapp(cache)
        || cache["numeric_generation"] != query_numeric_config_generation()
        || cache["skill_generation"] != SKILL_D->query_skill_config_generation()
        || cache["combat_exp"] != combat_exp
        || !intp(cache["cap"])
        || !mapp(cache["requirement_levels"]))
        return 0;

    requirement_levels = cache["requirement_levels"];
    foreach (basic_skill in keys(requirement_levels))
        if (SKILL_D->query_player_skill_level(player, basic_skill)
            != requirement_levels[basic_skill])
            return 0;
    return 1;
}
int query_skill_training_cap(object player, string skill_id)
{
    mapping cache;
    mapping def;
    mapping requirement_levels;
    mapping requirements;
    string skill_kind;
    int cap;
    int combat_exp;
    int def_cap;
    int max_level;
    int requirement_cap;

    if (!objectp(player) || !stringp(skill_id) || skill_id == "")
        return 0;

    combat_exp = player->query("combat_exp") || 0;
    cache = player->query_temp("numeric/skill_training_cap_cache/" + skill_id);
    if (skill_training_cap_cache_matches(player, cache, combat_exp)) {
        cap = cache["cap"];
        if (is_public_skill(skill_id) && cap > query_public_skill_cap())
            cap = query_public_skill_cap();
        return cap;
    }

    def = query_skill_numeric_definition(skill_id);
    if (!mapp(def))
        return 0;

    max_level = query_max_skill_level();
    cap = query_skill_cap_by_exp(combat_exp);
    def_cap = intp(def["max_level"]) && def["max_level"] > 0
        ? def["max_level"] : max_level;
    if (def_cap < cap)
        cap = def_cap;
    skill_kind = stringp(def["skill_kind"]) ? def["skill_kind"] : "";
    if (skill_kind == "")
        skill_kind = def["type"] == SKILL_CATEGORY_BASIC ? "basic" : "special";
    requirements = mapp(def["basic_requirements"])
        ? def["basic_requirements"] : ([ ]);
    requirement_levels = query_basic_requirement_levels(player, requirements);
    if (skill_kind == "special") {
        requirement_cap = query_basic_requirement_cap(
            requirements, requirement_levels);
        if (requirement_cap < cap)
            cap = requirement_cap;
    }

    if (cap > max_level)
        cap = max_level;
    if (is_public_skill(skill_id) && cap > query_public_skill_cap())
        cap = query_public_skill_cap();
    if (cap < 0)
        cap = 0;
    player->set_temp("numeric/skill_training_cap_cache/" + skill_id, ([
        "numeric_generation": query_numeric_config_generation(),
        "skill_generation": SKILL_D->query_skill_config_generation(),
        "combat_exp": combat_exp,
        "requirement_levels": requirement_levels,
        "cap": cap,
    ]));
    return cap;
}
int query_effective_skill_level(object player, string skill_id)
{
    int level;
    int cap;

    if (!objectp(player) || !stringp(skill_id) || skill_id == "")
        return 0;

    level = SKILL_D->query_player_skill_level(player, skill_id);
    cap = query_skill_training_cap(player, skill_id);
    if (level > cap)
        level = cap;
    return level < 0 ? 0 : level;
}
int query_skill_breakthrough_interval()
{
    return query_section_int("skill_breakthrough", "interval", 100);
}
int query_skill_breakthrough_level_by_tier(int tier)
{
    int *levels;
    int index;

    levels = query_skill_breakthrough_levels();
    index = tier - 1;
    if (index < 0 || index >= sizeof(levels))
        return 0;
    return levels[index];
}
string *query_public_skill_ids()
{
    mixed ids;
    mixed err;

    /* 内容清空阶段 core_breakthroughs 配置不存在，容错返回空列表。 */
    err = catch(ids = call_other(CORE_BREAKTHROUGH_CONFIG, "query_public_skill_ids"));
    if (err)
        return ({ });
    return arrayp(ids) ? ids + ({ }) : ({});
}
int is_public_skill(string skill_id)
{
    return stringp(skill_id) && skill_id != ""
        && member_array(skill_id, query_public_skill_ids()) != -1;
}
int query_public_skill_cap()
{
    return query_section_int("skill_breakthrough", "public_skill_cap", 100);
}
int query_skill_breakthrough_max_tier()
{
    return query_section_int("skill_breakthrough", "max_tier", 10);
}
int query_skill_breakthrough_release_max_tier()
{
    int max_tier;
    int release_max_tier;

    max_tier = query_skill_breakthrough_max_tier();
    release_max_tier = query_section_int(
        "skill_breakthrough", "release_max_tier", 1);
    if (release_max_tier > max_tier)
        release_max_tier = max_tier;
    return release_max_tier < 0 ? 0 : release_max_tier;
}
int query_skill_breakthrough_tier_by_level(int level)
{
    int *levels;
    int i;
    int tier;

    if (level <= 0)
        return 0;

    levels = query_skill_breakthrough_levels();
    if (sizeof(levels) == 0)
        return 0;

    tier = 0;
    for (i = 0; i < sizeof(levels); i++) {
        if (level >= levels[i])
            tier = i + 1;
        else
            break;
    }
    return tier;
}
int query_skill_breakthrough_tier(object player, string skill_id)
{
    return SKILL_BREAKTHROUGH_D->query_unlocked_tier(player, skill_id);
}
