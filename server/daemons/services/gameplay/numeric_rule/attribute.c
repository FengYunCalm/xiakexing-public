/**
 * @file    numeric_rule/attribute.c
 * @brief   派生属性与数值上下文
 *
 * @par Description:
 * 含 {query_numeric_context, query_context_factor_bp, apply_numeric_context_factor, query_primary_attribute, apply_scaled_weights, normalize_combat_attribute_delta, apply_derived_attribute_delta, apply_active_buff_effects, apply_static_combat_floors, query_derived_combat_attributes, apply_equipment_affix_stats, accumulate_affix_into, query_player_combat_attribute, query_player_max_resources, query_player_max_resource}。
 *
 * 本模块是 attribute 域纯规则片段，由 `numeric_rule_d.c` 继承；
 * 不持有 daemon 状态（nosave 全部留在 facade），
 * 配置经 facade 的 query_section/query_section_int 读取。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <game/skill.h>
#include <game_constants.h>
#include "defs.h"
int clamp_resource_value(int value);
string normalize_combat_attribute_key(string key);
varargs mapping query_numeric_effects(object player, string domain, object target);
mapping query_section(string section_name);
mapping query_weighted_category_skill_level_map(object player);
private void accumulate_affix_into(mapping attrs, object item);
private void apply_equipment_affix_stats(object player, mapping attrs);
mapping apply_numeric_context_factor(mapping values, mapping spec, object actor, object target);
varargs mapping query_derived_combat_attributes(object player, object target);
string query_numeric_context(object actor, object target);
int query_player_max_resource(object player, string resource);
mapping query_player_max_resources(object player);
string query_numeric_context(object actor, object target)
{
    if (objectp(actor) && objectp(target) && userp(actor) && userp(target))
        return "pvp";
    return "pve";
}
private int query_context_factor_bp(mapping spec, string context, mapping defaults)
{
    int factor;

    if (!mapp(spec))
        return BP_BASE;
    if (context == "pvp") {
        if (intp(spec["pvp_factor_bp"]) && spec["pvp_factor_bp"] > 0)
            return spec["pvp_factor_bp"];
        factor = mapp(defaults) && intp(defaults["default_pvp_factor_bp"]) ? defaults["default_pvp_factor_bp"] : BP_BASE;
        return factor > 0 ? factor : BP_BASE;
    }
    if (intp(spec["pve_factor_bp"]) && spec["pve_factor_bp"] > 0)
        return spec["pve_factor_bp"];
    factor = mapp(defaults) && intp(defaults["default_pve_factor_bp"]) ? defaults["default_pve_factor_bp"] : BP_BASE;
    return factor > 0 ? factor : BP_BASE;
}
mapping apply_numeric_context_factor(mapping values, mapping spec, object actor, object target)
{
    mapping defaults;
    mapping result;
    string context;
    string key;
    int factor_bp;
    int value;

    if (!mapp(values))
        return ([]);

    context = query_numeric_context(actor, target);
    defaults = query_section("skill_breakthrough");
    factor_bp = query_context_factor_bp(spec, context, defaults);
    result = ([]);
    foreach (key, value in values) {
        if (!stringp(key) || !intp(value))
            continue;
        result[key] = value * factor_bp / BP_BASE;
    }

    return result;
}
private int query_primary_attribute(object player, string attribute)
{
    if (!objectp(player) || !stringp(attribute) || attribute == "")
        return 10;

    switch (attribute) {
    case "strength":
        return player->query("attributes/strength") || player->query("str") || 10;
    case "constitution":
        return player->query("attributes/constitution") || player->query("con") || 10;
    case "dexterity":
        return player->query("attributes/dexterity") || player->query("dex") || 10;
    case "intelligence":
        return player->query("attributes/intelligence") || player->query("int") || 10;
    default:
        return 10;
    }
}
private void apply_scaled_weights(mapping result, int source_value, mapping weights, int divisor)
{
    string key;
    int weight;

    if (!mapp(result) || !mapp(weights) || source_value <= 0)
        return;
    if (divisor <= 0)
        divisor = 1;

    foreach (key, weight in weights) {
        if (!stringp(key) || !intp(weight))
            continue;
        result[key] = (result[key] || 0) + source_value * weight / divisor;
    }
}
private int normalize_combat_attribute_delta(string key, int value)
{
    if (key == "detection_pressure" || key == "negative_movement")
        return -value;
    return value;
}
private void apply_derived_attribute_delta(mapping result, string key, int value)
{
    value = normalize_combat_attribute_delta(key, value);
    key = normalize_combat_attribute_key(key);
    if (!stringp(key) || key == "")
        return;
    result[key] = (result[key] || 0) + value;
}
private void apply_active_buff_effects(object player, mapping result)
{
    mapping buff;
    mapping buffs;
    mapping effects;
    string buff_id;
    string key;
    int stacks;
    int value;

    if (!objectp(player) || !mapp(result) || !function_exists("query_buffs", player))
        return;

    if (function_exists("update_buffs", player))
        player->update_buffs();
    buffs = player->query_buffs();
    foreach (buff_id, buff in buffs || ([ ])) {
        if (!mapp(buff))
            continue;
        effects = buff["effects"];
        if (!mapp(effects) || sizeof(keys(effects)) == 0)
            continue;
        stacks = buff["stacks"] || 1;
        if (stacks <= 0)
            stacks = 1;
        foreach (key, value in effects) {
            if (!stringp(key) || !intp(value))
                continue;
            apply_derived_attribute_delta(result, key, value * stacks);
        }
    }
}
private void apply_static_combat_floors(object player, mapping result)
{
    string key;
    int value;

    foreach (key in ({ "max_hp", "max_mp", "max_ep", "attack", "defense", "parry" })) {
        value = player->query("combat/" + key);
        if (intp(value) && value > (result[key] || 0))
            result[key] = value;
    }
}
varargs mapping query_derived_combat_attributes(object player, object target)
{
    mapping config;
    mapping result;
    mapping weights;
    mapping effects;
    mapping skill_category_levels;
    string key;
    mixed raw_value;
    int skill_unit;

    if (!objectp(player))
        return ([]);

    config = query_section("derived_attributes");
    result = ([]);
    foreach (key, raw_value in config["base"] || ([])) {
        if (stringp(key) && intp(raw_value))
            result[key] = raw_value;
    }

    foreach (key, weights in config["attribute_weights"] || ([]))
        apply_scaled_weights(result, query_primary_attribute(player, key), weights, 1);

    skill_unit = config["skill_level_unit"] || 100;
    skill_category_levels = query_weighted_category_skill_level_map(player);
    foreach (key, weights in config["skill_category_weights"] || ([]))
        apply_scaled_weights(result, skill_category_levels[key] || 0, weights, skill_unit);

    effects = query_numeric_effects(player, "combat_attributes", target);
    foreach (key, raw_value in effects || ([])) {
        if (stringp(key) && intp(raw_value))
            apply_derived_attribute_delta(result, key, raw_value);
    }

    apply_static_combat_floors(player, result);
    apply_active_buff_effects(player, result);

    /* 装备词条（武器+护甲 affixes）：属性语义唯一所有者，全部消费方同源 */
    apply_equipment_affix_stats(player, result);

    foreach (key in ({ "max_hp", "max_mp", "max_ep" }))
        result[key] = clamp_resource_value(result[key] || 0);
    return result;
}
private void apply_equipment_affix_stats(object player, mapping attrs)
{
    object item;
    string slot;

    if (!objectp(player) || !mapp(attrs))
        return;
    item = player->query_temp("weapon");
    if (objectp(item))
        accumulate_affix_into(attrs, item);
    foreach (slot in EQUIPMENT_ARMOR_SLOTS) {
        item = player->query_temp("armor/" + slot);
        if (objectp(item))
            accumulate_affix_into(attrs, item);
    }
}
private void accumulate_affix_into(mapping attrs, object item)
{
    mixed *affixes;
    string stat;
    int value;

    if (!mapp(attrs) || !objectp(item))
        return;
    affixes = item->query("affixes");
    if (!arrayp(affixes))
        return;
    foreach (mapping affix in affixes) {
        stat = mapp(affix) ? (affix["stat"] || "") : "";
        value = mapp(affix) ? (affix["value"] || 0) : 0;
        if (stringp(stat) && stat != "" && intp(value) && value != 0)
            attrs[stat] = (attrs[stat] || 0) + value;
    }
}
int query_player_combat_attribute(object player, string attribute)
{
    mapping attrs;

    if (!objectp(player) || !stringp(attribute) || attribute == "")
        return 0;

    attrs = query_derived_combat_attributes(player);
    return attrs[normalize_combat_attribute_key(attribute)] || 0;
}
mapping query_player_max_resources(object player)
{
    mapping attrs;

    if (!objectp(player))
        return ([ "hp": 0, "mp": 0, "ep": 0 ]);

    attrs = query_derived_combat_attributes(player);
    return ([
        "hp": attrs["max_hp"] || 0,
        "mp": attrs["max_mp"] || 0,
        "ep": attrs["max_ep"] || 0,
    ]);
}
int query_player_max_resource(object player, string resource)
{
    mapping maxima;
    string key;

    if (!stringp(resource) || resource == "")
        return 0;

    switch (resource) {
    case "hp":
    case "max_hp":
        key = "hp";
        break;
    case "mp":
    case "max_mp":
        key = "mp";
        break;
    case "ep":
    case "max_ep":
        key = "ep";
        break;
    default:
        return 0;
    }

    maxima = query_player_max_resources(player);
    return maxima[key] || 0;
}
