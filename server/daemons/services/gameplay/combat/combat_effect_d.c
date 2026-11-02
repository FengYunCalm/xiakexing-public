/**
 * @file    combat_effect_d.c
 * @brief   战斗状态效果运行时服务



 *
 * @par Description:
 * 为控制、持续伤害、流血、内伤、破绽等 build 方向提供通用运行时骨架。
 * 状态账本复用 living 继承的 legacy-but-canonical buff_module，避免产生第二套状态存储。
 */

#pragma strict_types
#pragma save_types

#include <framework/message_facade.h>
#include <system/errors.h>
#include <system/services.h>
#include <system/xk_protocol.h>

inherit "/lib/framework/daemon";

#define BP_BASE 10000
#define COMBAT_MESSAGING_HELPER "/daemons/services/gameplay/combat/combat_messaging"

private mapping query_defaults()
{
    mapping framework;
    mapping defaults;

    framework = NUMERIC_RULE_D->query_combat_status_effect_framework();
    defaults = mapp(framework["defaults"]) ? framework["defaults"] : ([]);
    return defaults;
}

private int query_effect_int(mapping effect, mapping defaults, string key, int fallback)
{
    if (mapp(effect) && intp(effect[key]))
        return effect[key];
    if (mapp(defaults) && intp(defaults[key]))
        return defaults[key];
    return fallback;
}

private string query_effect_string(mapping effect, mapping defaults, string key, string fallback)
{
    if (mapp(effect) && stringp(effect[key]) && effect[key] != "")
        return effect[key];
    if (mapp(defaults) && stringp(defaults[key]) && defaults[key] != "")
        return defaults[key];
    return fallback;
}

private int query_effect_context_factor(mapping effect, object source, object target)
{
    string context;
    int factor;

    context = NUMERIC_RULE_D->query_numeric_context(source, target);
    if (context == "pvp")
        effect["damage_context"] = "pvp";
    if (objectp(source) && userp(source))
        effect["source_is_player"] = 1;
    if (context == "pvp") {
        factor = effect["pvp_factor_bp"] || BP_BASE;
        return factor > 0 ? factor : BP_BASE;
    }
    factor = effect["pve_factor_bp"] || BP_BASE;
    return factor > 0 ? factor : BP_BASE;
}

private void add_status_effect_delta(mapping values, string key, int value)
{
    if (!mapp(values) || !stringp(key) || key == "" || !intp(value) || value == 0)
        return;
    values[key] = (values[key] || 0) + value;
}

private string normalize_status_stat_key(string key)
{
    if (!stringp(key) || key == "")
        return "";

    switch (key) {
    case "damage_bonus":
        return "attack";
    case "hit_bonus":
    case "debuff_bonus":
    case "interrupt_bonus":
    case "poison_bonus":
        return "hit";
    case "dodge_bonus":
    case "move_speed":
    case "terrain_bonus":
    case "stealth_bonus":
    case "slow_bonus":
    case "lock_break_bonus":
    case "detection_pressure":
    case "negative_movement":
        return "dodge";
    case "parry_bonus":
    case "counter_bonus":
    case "pull_bonus":
    case "grab_bonus":
    case "stagger_bonus":
    case "line_pressure":
        return "parry";
    case "defense_bonus":
    case "guard_bonus":
    case "shield_bonus":
    case "field_bonus":
    case "knockback_bonus":
        return "defense";
    case "critical_bonus":
        return "critical";
    case "mark_bonus":
        return "critical_resist";
    case "control_resist":
    case "control_bonus":
    case "cleanse_bonus":
        return "control_resist_bp";
    case "armor_break_bonus":
        return "armor_break";
    default:
        return key;
    }
}

private int normalize_status_stat_value(string key, int value)
{
    if (key == "detection_pressure" || key == "negative_movement")
        return -value;
    return value;
}

private mapping normalize_status_stat_effects(mapping raw_stats, string effect_type)
{
    mapping stats;
    string key;
    string stat_key;
    int value;

    stats = ([ ]);
    foreach (key, value in raw_stats || ([ ])) {
        if (!stringp(key) || !intp(value) || value == 0)
            continue;
        if (key == "recover_hp" || key == "recover_mp" || key == "recover_ep")
            continue;
        if (key == "incoming_damage_reduction" || key == "incoming_combo_damage_reduction") {
            add_status_effect_delta(stats, "defense", -value);
            continue;
        }

        stat_key = normalize_status_stat_key(key);
        if (stat_key == "")
            continue;
        value = normalize_status_stat_value(key, value);
        if (effect_type == "debuff" && value > 0)
            value = -value;
        add_status_effect_delta(stats, stat_key, value);
    }
    return stats;
}

private mapping query_status_resource_healing(mapping raw_stats)
{
    mapping healing;
    string key;
    int value;

    healing = ([ ]);
    foreach (key, value in raw_stats || ([ ])) {
        if (!stringp(key) || !intp(value) || value <= 0)
            continue;
        switch (key) {
        case "recover_hp":
            add_status_effect_delta(healing, "hp", value);
            break;
        case "recover_mp":
            add_status_effect_delta(healing, "mp", value);
            break;
        case "recover_ep":
            add_status_effect_delta(healing, "ep", value);
            break;
        }
    }
    return healing;
}

mapping normalize_combat_status_effect(mapping raw_effect, object source, object target)
{
    mapping defaults;
    mapping effect;
    int round_seconds;
    int factor_bp;

    if (!mapp(raw_effect) || !stringp(raw_effect["id"]) || raw_effect["id"] == "")
        return 0;

    defaults = query_defaults();
    effect = copy(raw_effect);
    effect["combat_status"] = 1;
    effect["suppress_lifecycle_message"] = 1;
    effect["source"] = source;
    effect["remaining_rounds"] = query_effect_int(effect, defaults, "duration_rounds", 1);
    effect["tick_interval_rounds"] = query_effect_int(effect, defaults, "tick_interval_rounds", 1);
    effect["rounds_since_tick"] = 0;
    effect["stack_rule"] = query_effect_string(effect, defaults, "stack_rule", "refresh");
    effect["max_stacks"] = query_effect_int(effect, defaults, "max_stacks", 1);
    effect["pvp_factor_bp"] = query_effect_int(effect, defaults, "pvp_factor_bp", BP_BASE);
    effect["pve_factor_bp"] = query_effect_int(effect, defaults, "pve_factor_bp", BP_BASE);
    round_seconds = NUMERIC_RULE_D->query_combat_round_interval_seconds();
    if (round_seconds <= 0)
        round_seconds = 1;
    effect["duration"] = effect["remaining_rounds"] * round_seconds;
    effect["resource_healing"] = query_status_resource_healing(effect["stat_effects"]);
    effect["stat_effects"] = normalize_status_stat_effects(effect["stat_effects"], effect["type"] || "");
    effect["effects"] = mapp(effect["stat_effects"]) ? effect["stat_effects"] : ([ ]);
    factor_bp = query_effect_context_factor(effect, source, target);
    if (intp(effect["damage"]))
        effect["damage"] = effect["damage"] * factor_bp / BP_BASE;
    if (mapp(effect["resource_damage"]))
        effect["resource_damage"] = NUMERIC_RULE_D->apply_numeric_context_factor(effect["resource_damage"], effect, source, target);
    effect["budget"] = NUMERIC_RULE_D->query_combat_status_effect_budget_report(effect);
    effect = NUMERIC_RULE_D->apply_combat_status_effect_resistance(effect, target);
    if (objectp(target) && COMBAT_D->is_in_combat(target))
        effect["round_driven_lifecycle"] = 1;
    return effect;
}

private string query_status_effect_name(mapping effect)
{
    if (mapp(effect) && stringp(effect["name"]) && effect["name"] != "")
        return effect["name"];
    if (mapp(effect) && stringp(effect["id"]) && effect["id"] != "")
        return effect["id"];
    return "状态效果";
}

private mapping build_status_effect_packet(mapping effect)
{
    if (!mapp(effect))
        return ([]);
    return ([
        "id": effect["id"] || "",
        "name": query_status_effect_name(effect),
        "type": effect["type"] || "",
        "remaining_rounds": effect["remaining_rounds"] || 0,
        "duration": effect["duration"] || 0,
        "stacks": effect["stacks"] || 1,
    ]);
}

private string query_combat_status_category(mapping effect)
{
    string type;

    if (!mapp(effect))
        return "marker";
    type = effect["type"] || "";
    switch (type) {
    case "control":
        return "control";
    case "buff":
    case "debuff":
    case "dot":
    case "poison":
    case "bleed":
    case "burn":
        return "effect";
    }
    if (mapp(effect["stat_effects"]) && sizeof(effect["stat_effects"]) > 0)
        return "effect";
    if (mapp(effect["effects"]) && sizeof(effect["effects"]) > 0)
        return "effect";
    if (intp(effect["damage"]) && effect["damage"] != 0)
        return "effect";
    if (mapp(effect["resource_damage"]) && sizeof(effect["resource_damage"]) > 0)
        return "effect";
    return "marker";
}

private int query_combat_status_remaining_seconds(mapping effect)
{
    int rounds;
    int round_seconds;

    if (!mapp(effect))
        return 0;
    if (intp(effect["duration"]) && effect["duration"] > 0)
        return effect["duration"];
    rounds = effect["remaining_rounds"] || effect["duration_rounds"] || 0;
    if (rounds <= 0)
        return 0;
    round_seconds = NUMERIC_RULE_D->query_combat_round_interval_seconds();
    if (round_seconds <= 0)
        round_seconds = 1;
    return rounds * round_seconds;
}

mapping build_combat_status_instance(mapping effect, string owner)
{
    mapping instance;
    string id;

    if (!mapp(effect))
        return ([]);
    id = effect["id"] || query_status_effect_name(effect);
    instance = ([
        "id": id,
        "name": query_status_effect_name(effect),
        "category": query_combat_status_category(effect),
        "type": effect["type"] || "",
        "owner": stringp(owner) && owner != "" ? owner : "target",
        "remaining_seconds": query_combat_status_remaining_seconds(effect),
        "remaining_rounds": effect["remaining_rounds"] || effect["duration_rounds"] || 0,
        "stacks": effect["stacks"] || 1,
        "visible": intp(effect["visible"]) ? effect["visible"] : 1,
        "source_action": effect["source_action"] || effect["action_id"] || "",
    ]);
    if (arrayp(effect["tags"]))
        instance["tags"] = effect["tags"];
    return instance;
}

mixed *build_combat_status_instances(mixed *effects, string owner)
{
    mixed *instances;
    mapping effect;
    int index;

    if (!arrayp(effects) || sizeof(effects) == 0)
        return ({ });
    instances = ({ });
    for (index = 0; index < sizeof(effects); index++) {
        if (!mapp(effects[index]))
            continue;
        effect = effects[index];
        instances += ({ build_combat_status_instance(effect, owner) });
    }
    return instances;
}

private mixed *build_status_effect_packets(mixed *effects)
{
    mixed *packets;
    mapping effect;
    int index;

    if (!arrayp(effects) || sizeof(effects) == 0)
        return ({ });
    packets = allocate(sizeof(effects));
    for (index = 0; index < sizeof(effects); index++) {
        effect = mapp(effects[index]) ? effects[index] : ([]);
        packets[index] = build_status_effect_packet(effect);
    }
    return packets;
}

private string *query_unique_status_effect_names(mixed *effects)
{
    string *names;
    mapping seen;
    string name;
    int index;

    if (!arrayp(effects) || sizeof(effects) == 0)
        return ({ });
    names = ({ });
    seen = ([ ]);
    for (index = 0; index < sizeof(effects); index++) {
        name = query_status_effect_name(effects[index]);
        if (seen[name])
            continue;
        seen[name] = 1;
        names += ({ name });
    }
    return names;
}

private mixed *dedupe_status_lifecycle_effects(mixed *effects)
{
    mapping seen;
    mixed *deduped;
    mixed raw_effect;
    string name;

    if (!arrayp(effects) || sizeof(effects) == 0)
        return ({ });
    seen = ([ ]);
    deduped = ({ });
    foreach (raw_effect in effects) {
        name = query_status_effect_name(raw_effect);
        if (seen[name])
            continue;
        seen[name] = 1;
        deduped += ({ raw_effect });
    }
    return deduped;
}

private string build_status_lifecycle_message(string event, mixed *effects)
{
    int count;
    string *unique_names;
    string names;

    unique_names = query_unique_status_effect_names(effects);
    count = sizeof(unique_names);
    if (count <= 0)
        return "";
    names = implode(unique_names, "、");
    switch (event) {
    case "status_apply":
        return count == 1 ? sprintf("你获得了「%s」效果。", names)
            : sprintf("你获得了%d个状态效果：%s。", count, names);
    case "status_expire":
        return count == 1 ? sprintf("你的「%s」效果消失了。", names)
            : sprintf("你的%d个状态效果消失了：%s。", count, names);
    case "status_clear":
        return count == 1 ? sprintf("战斗结束，「%s」效果散去。", names)
            : sprintf("战斗结束，%d个状态效果散去：%s。", count, names);
    }
    return "";
}

private mixed *filter_status_lifecycle_effects(string event, mixed *effects)
{
    mapping effect;
    mixed *filtered;
    mixed raw_effect;

    if (!arrayp(effects) || sizeof(effects) == 0)
        return ({ });
    if (event != "status_apply")
        return effects;

    filtered = ({ });
    foreach (raw_effect in effects) {
        effect = mapp(raw_effect) ? raw_effect : 0;
        if (mapp(effect) && effect["suppress_apply_lifecycle_message"])
            continue;
        filtered += ({ raw_effect });
    }
    return filtered;
}

private void send_status_lifecycle_packet(object target, string event, mixed *effects)
{
    string message;

    if (!objectp(target) || !stringp(event) || event == ""
        || !arrayp(effects) || sizeof(effects) == 0)
        return;
    effects = filter_status_lifecycle_effects(event, effects);
    effects = dedupe_status_lifecycle_effects(effects);
    if (sizeof(effects) == 0)
        return;
    message = build_status_lifecycle_message(event, effects);
    if (message == "")
        return;
    COMBAT_D->combat_packet(target, XK_CBRD, ([
        "event": event,
        "target": target->query_name() || target->query("name") || "你",
        "message": message,
        "result_label": "状态变化",
        "status_effects": build_status_effect_packets(effects),
        "statuses": build_combat_status_instances(effects, "self"),
    ]));
}

private mapping add_normalized_combat_status_effect(object target, mapping effect)
{
    int code;
    int repeated_application;

    if (!objectp(target) || !mapp(effect))
        return ([ "ok": 0, "error": "invalid_arguments" ]);
    if (!function_exists("add_buff", target))
        return ([ "ok": 0, "error": "target_has_no_buff_module" ]);

    repeated_application = function_exists("query_buff", target)
        && mapp(target->query_buff(effect["id"]));
    code = target->add_buff(effect["id"], effect);
    if (code != ERR_OK)
        return ([ "ok": 0, "error": "add_buff_failed", "code": code ]);
    return ([
        "ok": 1,
        "effect_id": effect["id"],
        "type": effect["type"] || "",
        "repeated_application": repeated_application,
    ]);
}

mapping apply_combat_status_effect(object source, object target, mapping raw_effect)
{
    mapping effect;
    mapping result;

    if (!objectp(target) || !mapp(raw_effect))
        return ([ "ok": 0, "error": "invalid_arguments" ]);

    effect = normalize_combat_status_effect(raw_effect, source, target);
    if (!mapp(effect))
        return ([ "ok": 0, "error": "invalid_effect" ]);
    result = add_normalized_combat_status_effect(target, effect);
    if (mapp(result) && result["ok"] && !result["repeated_application"])
        send_status_lifecycle_packet(target, "status_apply", ({ effect }));
    return result;
}

mapping apply_combat_status_effects(object source, object target, mixed *raw_effects)
{
    mapping result;
    mapping effect;
    mixed *applied_effects;
    mixed raw_effect;
    int applied;

    if (!arrayp(raw_effects) || sizeof(raw_effects) == 0)
        return ([ "ok": 1, "applied": 0 ]);

    applied_effects = ({ });
    foreach (raw_effect in raw_effects) {
        if (!mapp(raw_effect))
            continue;
        effect = normalize_combat_status_effect(raw_effect, source, target);
        if (!mapp(effect))
            continue;
        result = add_normalized_combat_status_effect(target, effect);
        if (mapp(result) && result["ok"]) {
            applied++;
            if (!result["repeated_application"])
                applied_effects += ({ effect });
        }
    }

    send_status_lifecycle_packet(target, "status_apply", applied_effects);

    return ([ "ok": 1, "applied": applied ]);
}

int query_combat_status_stacks(object target, string effect_id)
{
    mapping effect;

    if (!objectp(target) || !stringp(effect_id) || effect_id == ""
        || !function_exists("query_buff", target))
        return 0;
    effect = target->query_buff(effect_id);
    if (!mapp(effect) || !effect["combat_status"])
        return 0;
    return effect["stacks"] > 0 ? effect["stacks"] : 1;
}

mapping consume_combat_status_stacks(object target, string effect_id, int count)
{
    mapping effect;
    int remaining;

    if (!objectp(target) || !stringp(effect_id) || effect_id == "" || count <= 0
        || !function_exists("query_buff", target) || !function_exists("remove_buff", target))
        return ([ "ok": 0, "error": "invalid_arguments" ]);
    effect = target->query_buff(effect_id);
    if (!mapp(effect) || !effect["combat_status"])
        return ([ "ok": 0, "error": "missing_status" ]);
    if ((effect["stacks"] || 1) < count)
        return ([ "ok": 0, "error": "insufficient_stacks" ]);

    remaining = (effect["stacks"] || 1) - count;
    if (!target->remove_buff(effect_id))
        return ([ "ok": 0, "error": "remove_failed" ]);
    if (remaining > 0) {
        effect = copy(effect);
        effect["stacks"] = remaining;
        if (target->add_buff(effect_id, effect) != ERR_OK)
            return ([ "ok": 0, "error": "restore_failed" ]);
    }
    return ([ "ok": 1, "effect_id": effect_id,
        "consumed_stacks": count, "remaining_stacks": remaining ]);
}

private int effect_blocks_action(mapping effect)
{
    string type;

    if (!mapp(effect))
        return 0;
    if (effect["action_block"])
        return 1;
    type = effect["type"] || "";
    return type == "control" ? 1 : 0;
}

private mapping apply_effect_damage(object target, mapping effect, int nonlethal)
{
    mapping damage_report;
    mapping protection;
    mapping result;
    mapping resource_damage;
    mapping resource_healing;
    object source;
    string resource;
    int amount;
    int hp;

    result = ([ "damage": 0, "defeated": 0 ]);

    if (!objectp(target) || !mapp(effect))
        return result;

    source = effect["source"];
    amount = effect["damage"] || 0;
    if (amount > 0 && function_exists("receive_damage", target)) {
        if (amount > 0) {
            protection = NUMERIC_RULE_D->apply_combat_pvp_damage_protection(
                source, target, amount, ([
                    "damage_source": "status_effect",
                    "damage_context": effect["damage_context"] || "",
                    "source_is_player": effect["source_is_player"],
                    "effect_id": effect["id"] || "",
                    "effect_type": effect["type"] || "",
                ]));
            if (mapp(protection) && intp(protection["applied_damage"]))
                amount = protection["applied_damage"];
        }
        hp = target->query("combat/hp") || 0;
        result["defeated"] = amount > 0 && hp <= amount;
        if (nonlethal && result["defeated"])
            amount = hp > 1 ? hp - 1 : 0;
        if (amount > 0) {
            target->receive_damage("combat/hp", amount, source);
            result["damage"] += amount;
            if (!objectp(target)) {
                if (!result["defeated"])
                    error("Status target disappeared without a defeat result.\n");
                return result;
            }
            damage_report = mapp(protection) ? copy(protection) : ([ ]);
            damage_report["event"] = "status_effect";
            damage_report["damage_source"] = "status_effect";
            damage_report["effect_id"] = effect["id"] || "";
            damage_report["effect_type"] = effect["type"] || "";
            damage_report["effect_name"] = effect["name"] || "";
            damage_report["applied_damage"] = amount;
            if (objectp(source))
                source->set_temp("combat/last_pvp_damage_protection", protection);
            target->set_temp("combat/last_received_pvp_damage_protection", protection);
            call_other(COMBAT_MESSAGING_HELPER, "send_damage_packets",
                source, target, amount, target->query("combat/hp") || 0, damage_report);
            result["defeated"] = (nonlethal && result["defeated"])
                || (target->query("combat/hp") || 0) <= 0;
        }
    }
    if (result["defeated"])
        return result;

    resource_damage = effect["resource_damage"];
    foreach (resource, amount in resource_damage || ([ ])) {
        if (!intp(amount) || amount <= 0)
            continue;
        if (resource == "mp" && function_exists("consume_mp", target))
            target->consume_mp(amount);
        else if (resource == "ep" && function_exists("consume_ep", target))
            target->consume_ep(amount);
        result["damage"] += amount;
    }

    resource_healing = effect["resource_healing"];
    foreach (resource, amount in resource_healing || ([ ])) {
        if (!intp(amount) || amount <= 0)
            continue;
        if (resource == "hp" && function_exists("heal_hp", target))
            target->heal_hp(amount);
        else if (resource == "mp" && function_exists("heal_mp", target))
            target->heal_mp(amount);
        else if (resource == "ep" && function_exists("heal_ep", target))
            target->heal_ep(amount);
    }

    return result;
}

varargs mapping process_combat_status_effects(object target, object opponent, int nonlethal)
{
    mapping buff;
    mapping buffs;
    mapping result;
    mapping tick_result;
    mixed *expired_effects;
    string *expired_ids;
    string buff_id;
    int blocked;
    int damage;
    int tick_interval;

    if (!objectp(target) || !function_exists("query_buffs", target))
        return ([ "ok": 1, "action_blocked": 0, "damage": 0 ]);

    if (function_exists("update_buffs", target))
        target->update_buffs();

    buffs = target->query_buffs();
    result = ([ "ok": 1, "action_blocked": 0, "damage": 0, "defeated": 0 ]);
    expired_ids = ({ });
    expired_effects = ({ });
    foreach (buff_id, buff in buffs || ([ ])) {
        if (!mapp(buff) || !buff["combat_status"])
            continue;

        if (effect_blocks_action(buff))
            blocked = 1;
        tick_interval = buff["tick_interval_rounds"] || 1;
        buff["rounds_since_tick"] = (buff["rounds_since_tick"] || 0) + 1;
        if (tick_interval <= 1 || buff["rounds_since_tick"] >= tick_interval) {
            tick_result = apply_effect_damage(target, buff, nonlethal);
            damage += mapp(tick_result) ? (tick_result["damage"] || 0) : 0;
            if (mapp(tick_result) && tick_result["defeated"]) {
                result["defeated"] = 1;
                result["damage"] = damage;
                return result;
            }
            buff["rounds_since_tick"] = 0;
        }

        buff["remaining_rounds"] = (buff["remaining_rounds"] || 1) - 1;
        if ((buff["remaining_rounds"] || 0) <= 0) {
            expired_ids += ({ buff_id });
            expired_effects += ({ copy(buff) });
        } else
            buffs[buff_id] = buff;
    }

    foreach (buff_id in expired_ids)
        target->remove_buff(buff_id);
    send_status_lifecycle_packet(target, "status_expire", expired_effects);

    target->set_temp("buffs", target->query_buffs());
    result["action_blocked"] = blocked;
    result["damage"] = damage;
    return result;
}

void clear_combat_status_effects(object target)
{
    mapping buff;
    mapping buffs;
    mixed *cleared_effects;
    string buff_id;

    if (!objectp(target) || !function_exists("query_buffs", target)
        || !function_exists("remove_buff", target))
        return;

    buffs = target->query_buffs();
    cleared_effects = ({ });
    foreach (buff_id, buff in buffs || ([ ])) {
        if (mapp(buff) && buff["combat_status"]) {
            cleared_effects += ({ copy(buff) });
            target->remove_buff(buff_id);
        }
    }
    send_status_lifecycle_packet(target, "status_clear", cleared_effects);
}

int is_action_blocked(object target)
{
    mapping buff;
    string buff_id;

    if (!objectp(target) || !function_exists("query_buffs", target))
        return 0;

    foreach (buff_id, buff in target->query_buffs() || ([ ])) {
        if (mapp(buff) && buff["combat_status"] && effect_blocks_action(buff))
            return 1;
    }
    return 0;
}
