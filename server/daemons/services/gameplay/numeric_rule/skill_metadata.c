/**
 * @file    numeric_rule/skill_metadata.c
 * @brief   技能数值元数据
 *
 * @par Description:
 * 含 {merge_combat_resource_impact, query_skill_category_from_definition, query_skill_kind_from_definition, merge_breakthrough_entry_numeric_metadata, merge_breakthrough_numeric_metadata, query_skill_numeric_definition, skill_breakthrough_numeric_domain_supported, query_combat_resource_impact, query_primary_attribute_resource_impact, query_skill_breakthrough_definitions, query_skill_breakthrough_candidates}。
 *
 * 本模块是 skill_metadata 域纯规则片段，由 `numeric_rule_d.c` 继承；
 * 不持有 daemon 状态（nosave 全部留在 facade），
 * 配置经 facade 的 query_section/query_section_int 读取。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <game/skill.h>
#include <game_constants.h>
#include "defs.h"
mapping normalize_breakthrough_entry(string skill_id, mixed raw_tier, mapping raw_entry);
string normalize_combat_attribute_key(string key);
mapping query_section(string section_name);
mapping query_skill_numeric_metadata(string skill_id);
 void merge_combat_resource_impact(mapping result, mapping effects)
{
    string key;
    string normalized_key;
    int value;

    if (!mapp(result) || !mapp(effects))
        return;
    foreach (key, value in effects) {
        if (!stringp(key) || !intp(value) || value == 0)
            continue;
        normalized_key = normalize_combat_attribute_key(key);
        if (normalized_key == "max_hp")
            result["hp"] = 1;
        else if (normalized_key == "max_mp")
            result["mp"] = 1;
        else if (normalized_key == "max_ep")
            result["ep"] = 1;
    }
}
 string query_skill_category_from_definition(mapping def)
{
    if (!mapp(def))
        return "";
    if (stringp(def["numeric_category"]) && def["numeric_category"] != "")
        return def["numeric_category"];
    if (stringp(def["category"]) && def["category"] != "")
        return def["category"];
    if (stringp(def["type"]) && def["type"] != "")
        return def["type"];
    return "";
}
 string query_skill_kind_from_definition(mapping def)
{
    if (!mapp(def))
        return "";
    if (stringp(def["skill_kind"]) && def["skill_kind"] != "")
        return def["skill_kind"];
    return def["type"] == SKILL_CATEGORY_BASIC ? "basic" : "special";
}
private void merge_breakthrough_entry_numeric_metadata(mapping domains,
    mapping resource_impact, mapping entry)
{
    mapping domain_effects;
    mapping effects;
    string domain;

    if (!mapp(domains) || !mapp(resource_impact) || !mapp(entry))
        return;
    domain_effects = mapp(entry["numeric_effects"])
        ? entry["numeric_effects"] : 0;
    if (mapp(domain_effects)) {
        foreach (domain, effects in domain_effects) {
            if (!stringp(domain) || domain == "" || !mapp(effects))
                continue;
            domains[domain] = 1;
            if (domain == "combat_attributes")
                merge_combat_resource_impact(resource_impact, effects);
        }
        return;
    }

    domain = stringp(entry["domain"]) ? entry["domain"] : "";
    effects = mapp(entry["effects"]) ? entry["effects"] : 0;
    if (domain == "" || !mapp(effects))
        return;
    domains[domain] = 1;
    if (domain == "combat_attributes")
        merge_combat_resource_impact(resource_impact, effects);
}
 void merge_breakthrough_numeric_metadata(mapping domains,
    mapping resource_impact, mixed raw_breakthroughs)
{
    mixed raw_entry;
    mixed raw_key;
    mixed raw_value;

    if (arrayp(raw_breakthroughs)) {
        foreach (raw_entry in raw_breakthroughs)
            if (mapp(raw_entry))
                merge_breakthrough_entry_numeric_metadata(domains,
                    resource_impact, raw_entry);
        return;
    }
    if (!mapp(raw_breakthroughs))
        return;
    foreach (raw_key, raw_value in raw_breakthroughs) {
        if (mapp(raw_value)) {
            merge_breakthrough_entry_numeric_metadata(domains,
                resource_impact, raw_value);
            continue;
        }
        if (!arrayp(raw_value))
            continue;
        foreach (raw_entry in raw_value)
            if (mapp(raw_entry))
                merge_breakthrough_entry_numeric_metadata(domains,
                    resource_impact, raw_entry);
    }
}
 mapping query_skill_numeric_definition(string skill_id)
{
    mapping metadata;

    metadata = query_skill_numeric_metadata(skill_id);
    return mapp(metadata["definition"]) ? metadata["definition"] : 0;
}
 int skill_breakthrough_numeric_domain_supported(string skill_id,
    string domain)
{
    mapping domains;
    mapping metadata;

    metadata = query_skill_numeric_metadata(skill_id);
    domains = mapp(metadata["breakthrough_numeric_domains"])
        ? metadata["breakthrough_numeric_domains"] : 0;
    return mapp(domains) && domains[domain] ? 1 : 0;
}
mapping query_combat_resource_impact(mapping effects)
{
    mapping result;

    result = ([ "hp": 0, "mp": 0, "ep": 0 ]);
    merge_combat_resource_impact(result, effects);
    return result;
}
mapping query_primary_attribute_resource_impact(string attribute)
{
    mapping config;
    mapping weights;

    config = query_section("derived_attributes");
    weights = mapp(config["attribute_weights"])
        ? config["attribute_weights"][attribute] : 0;
    return query_combat_resource_impact(weights);
}
mapping *query_skill_breakthrough_definitions(string skill_id)
{
    mapping def;
    mapping normalized;
    mapping *entries;
    mixed raw_entries;
    mixed raw_entry;
    mixed raw_key;
    mixed raw_value;

    if (!stringp(skill_id) || skill_id == "")
        return ({ });

    def = query_skill_numeric_definition(skill_id);
    raw_entries = mapp(def) ? def["breakthroughs"] : 0;
    entries = ({ });
    if (mapp(raw_entries)) {
        foreach (raw_key, raw_value in raw_entries) {
            if (mapp(raw_value)) {
                normalized = normalize_breakthrough_entry(skill_id, raw_key, raw_value);
                if (mapp(normalized))
                    entries += ({ normalized });
                continue;
            }
            if (arrayp(raw_value)) {
                foreach (raw_entry in raw_value) {
                    if (!mapp(raw_entry))
                        continue;
                    normalized = normalize_breakthrough_entry(skill_id, raw_key, raw_entry);
                    if (mapp(normalized))
                        entries += ({ normalized });
                }
            }
        }
    } else if (arrayp(raw_entries)) {
        foreach (raw_entry in raw_entries) {
            if (!mapp(raw_entry))
                continue;
            normalized = normalize_breakthrough_entry(skill_id, raw_entry["tier"], raw_entry);
            if (mapp(normalized))
                entries += ({ normalized });
        }
    }

    return entries;
}
mapping *query_skill_breakthrough_candidates(string skill_id, int tier)
{
    mapping entry;
    mapping *candidates;
    mapping *entries;

    candidates = ({ });
    if (!stringp(skill_id) || skill_id == "" || tier <= 0)
        return candidates;

    entries = query_skill_breakthrough_definitions(skill_id);
    foreach (entry in entries || ({ })) {
        if (!mapp(entry) || (entry["tier"] || 0) != tier)
            continue;
        candidates += ({ copy(entry) });
    }
    return candidates;
}
