/**
 * @file    combat_messaging.c
 * @brief   战斗消息协议辅助子服务



 */

#pragma strict_types
#pragma save_types

#include <system/ansi.h>
#include <system/xk_protocol.h>
#include <framework/message_facade.h>
#include <system/services.h>

#define DAEMON_BRIDGE "/lib/framework/daemon"

private string query_living_name(object who, string fallback)
{
    if (!stringp(fallback) || fallback == "")
        fallback = "对手";
    if (!objectp(who))
        return fallback;
    return who->query_name() || who->query("name") || fallback;
}

private int query_combat_resource(object who, string key)
{
    int value;

    if (!objectp(who) || !stringp(key) || key == "")
        return -1;
    value = who->query("combat/" + key);
    return intp(value) ? value : -1;
}

private string query_skill_display_name(string skill_id, string fallback)
{
    mapping definition;

    if (!stringp(fallback) || fallback == "")
        fallback = "招式";
    if (!stringp(skill_id) || skill_id == "")
        return fallback;

    definition = SKILL_D->get_skill_definition(skill_id);
    if (mapp(definition) && stringp(definition["name"]) && definition["name"] != "")
        return definition["name"];
    return fallback;
}

private string query_effect_display_name(string effect_type, string effect_id)
{
    switch (effect_type || "") {
    case "control":
        return "压制劲力";
    case "dot":
        return "持续伤势";
    case "debuff":
        return "滞涩劲力";
    case "poison":
        return "毒劲";
    case "bleed":
        return "流血伤势";
    case "burn":
        return "灼烧之势";
    }

    if (stringp(effect_id) && effect_id != "") {
        if (strsrch(effect_id, "poison") != -1)
            return "毒劲";
        if (strsrch(effect_id, "bleed") != -1)
            return "流血伤势";
        if (strsrch(effect_id, "burn") != -1)
            return "灼烧之势";
        if (strsrch(effect_id, "control") != -1)
            return "压制劲力";
    }

    return "状态效果";
}

private string format_action_label(string skill_name, string action_name)
{
    if (!stringp(action_name) || action_name == "")
        action_name = skill_name;
    if (!stringp(skill_name) || skill_name == "" || skill_name == action_name)
        return sprintf("「%s」", action_name != "" ? action_name : "招式");
    return sprintf("「%s·%s」", skill_name, action_name);
}

private string query_action_display_name(string skill_name, string action_name)
{
    if (stringp(action_name) && action_name != "")
        return action_name;
    if (stringp(skill_name) && skill_name != "")
        return skill_name;
    return "普通攻击";
}

private void copy_display_fields(mapping payload, mapping details)
{
    string effect_name;
    string skill_name;

    if (!mapp(payload) || !mapp(details))
        return;

    if (stringp(details["message"]) && details["message"] != "")
        payload["message"] = details["message"];
    if (stringp(details["detail_message"]) && details["detail_message"] != "")
        payload["detail_message"] = details["detail_message"];
    if (stringp(details["action_name"]) && details["action_name"] != "")
        payload["action_name"] = details["action_name"];
    if (stringp(details["result_label"]) && details["result_label"] != "")
        payload["result_label"] = details["result_label"];

    skill_name = details["skill_name"] || "";
    if (skill_name == "" && stringp(details["skill_id"]) && details["skill_id"] != "")
        skill_name = query_skill_display_name(details["skill_id"], "招式");
    if (skill_name != "")
        payload["skill_name"] = skill_name;

    effect_name = details["effect_name"] || "";
    if (effect_name == "")
        effect_name = query_effect_display_name(details["effect_type"] || "", details["effect_id"] || "");
    if (effect_name != "")
        payload["effect_name"] = effect_name;
}

private string build_action_damage_message(string source_name, string target_name,
    int final_damage, int defender_hp, string action_message,
    int source_is_viewer)
{
    string message;
    string result_target;

    message = replace_string(action_message, "\r", "");
    message = replace_string(message, "\n", "");
    if (source_is_viewer)
        message = replace_string(message, "目标", target_name);
    else {
        message = replace_string(message, "你", source_name);
        message = replace_string(message, "目标", "你");
    }
    result_target = source_is_viewer ? target_name : "你";
    return sprintf("%s\n造成%d点伤害，%s剩余%d点气血。",
        message, final_damage, result_target, defender_hp);
}

private string build_damage_message(string event, string source_name,
    string target_name, int final_damage, int defender_hp, mapping details,
    int source_is_viewer)
{
    string skill_name;
    string effect_name;
    string action_name;
    string action_label;
    string action_text;
    string hp_text;

    if (mapp(details) && stringp(details["message"]) && details["message"] != "") {
        if (event == "basic_attack" || event == "perform" || event == "exert")
            return build_action_damage_message(source_name, target_name,
                final_damage, defender_hp, details["message"], source_is_viewer);
        if (event != "npc_skill")
            return details["message"];
    }

    skill_name = mapp(details) ? (details["skill_name"] || "") : "";
    if (skill_name == "" && mapp(details))
        skill_name = query_skill_display_name(details["skill_id"] || "", "招式");
    action_name = mapp(details) ? (details["action_name"] || "") : "";
    if (action_name == "")
        action_name = skill_name;
    effect_name = mapp(details) ? (details["effect_name"] || "") : "";
    if (effect_name == "" && mapp(details))
        effect_name = query_effect_display_name(details["effect_type"] || "", details["effect_id"] || "");

    action_label = format_action_label(skill_name, action_name);
    action_text = query_action_display_name(skill_name, action_name);
    hp_text = defender_hp >= 0 ? sprintf("，%s剩余%d点气血", target_name, defender_hp) : "";

    switch (event) {
    case "status_effect":
        return sprintf("%s持续作用于%s，造成%d点伤害%s。", effect_name, target_name, final_damage, hp_text);
    case "npc_skill":
        return sprintf("%s使出%s，命中%s，造成%d点伤害%s。", source_name, action_label,
            target_name, final_damage, hp_text);
    case "perform":
        return source_is_viewer
            ? sprintf("（你的%s对%s造成%d伤害）", action_text, target_name, final_damage)
            : sprintf("（%s的%s对你造成%d伤害）", source_name, action_text, final_damage);
    case "exert":
        return source_is_viewer
            ? sprintf("（你的%s对%s造成%d伤害）", action_text, target_name, final_damage)
            : sprintf("（%s的%s对你造成%d伤害）", source_name, action_text, final_damage);
    case "basic_attack":
        return source_is_viewer
            ? sprintf("你：%s -> %s：%d", action_text, target_name, final_damage)
            : sprintf("%s：%s -> 你：%d", source_name, action_text, final_damage);
    default:
        return source_is_viewer
            ? sprintf("你：%s -> %s：%d", action_text, target_name, final_damage)
            : sprintf("%s：%s -> 你：%d", source_name, action_text, final_damage);
    }
}

private string build_damage_detail_message(int final_damage, mapping details)
{
    if (mapp(details) && stringp(details["detail_message"]) && details["detail_message"] != "")
        return details["detail_message"];
    if (mapp(details) && details["protection_triggered"])
        return sprintf("这一击原本可造成%d点伤害，被较量保护卸去%d点，最终落下%d点。",
            details["raw_damage"] || final_damage,
            details["prevented_damage"] || 0,
            final_damage);
    return "";
}

private mapping build_damage_payload(string event, string source_name, string target_name,
    int final_damage, object defender, int defender_hp, mapping details, int source_is_viewer)
{
    mapping payload;
    mapping status;
    mixed *statuses;
    string detail_message;
    string message;

    payload = ([
        "event": event,
        "source": source_is_viewer ? "你" : source_name,
        "target": source_is_viewer ? target_name : "你",
        "damage": final_damage,
        "target_hp": defender_hp,
        "target_max_hp": query_combat_resource(defender, "max_hp"),
        "target_mp": query_combat_resource(defender, "mp"),
        "target_max_mp": query_combat_resource(defender, "max_mp"),
        "target_ep": query_combat_resource(defender, "ep"),
        "target_max_ep": query_combat_resource(defender, "max_ep")
    ]);
    copy_display_fields(payload, details);
    message = build_damage_message(event, source_name, target_name, final_damage,
        defender_hp, details, source_is_viewer);
    if (message != "")
        payload["message"] = message;
    detail_message = build_damage_detail_message(final_damage, details);
    if (detail_message != "")
        payload["detail_message"] = detail_message;
    if (mapp(details)) {
        if (intp(details["raw_damage"]))
            payload["raw_damage"] = details["raw_damage"];
        if (intp(details["prevented_damage"]))
            payload["prevented_damage"] = details["prevented_damage"];
        if (intp(details["protection_triggered"]))
            payload["protection_triggered"] = details["protection_triggered"];
        if (stringp(details["protection_reason"]) && details["protection_reason"] != "")
            payload["protection_reason"] = details["protection_reason"];
        if (intp(details["single_hit_soft_damage"]))
            payload["single_hit_soft_damage"] = details["single_hit_soft_damage"];
        if (intp(details["single_hit_overflow_reduce_bp"]))
            payload["single_hit_overflow_reduce_bp"] = details["single_hit_overflow_reduce_bp"];
        if (intp(details["window_soft_damage"]))
            payload["window_soft_damage"] = details["window_soft_damage"];
        if (intp(details["window_overflow_reduce_bp"]))
            payload["window_overflow_reduce_bp"] = details["window_overflow_reduce_bp"];
        if (intp(details["window_raw_total_damage"]))
            payload["window_raw_total_damage"] = details["window_raw_total_damage"];
        if (intp(details["window_total_damage"]))
            payload["window_total_damage"] = details["window_total_damage"];
        if (intp(details["window_remaining_damage"]))
            payload["window_remaining_damage"] = details["window_remaining_damage"];
        if (stringp(details["damage_source"]) && details["damage_source"] != "")
            payload["damage_source"] = details["damage_source"];
        if (stringp(details["skill_id"]) && details["skill_id"] != "")
            payload["skill_id"] = details["skill_id"];
        if (stringp(details["effect_id"]) && details["effect_id"] != "")
            payload["effect_id"] = details["effect_id"];
        if (stringp(details["effect_type"]) && details["effect_type"] != "")
            payload["effect_type"] = details["effect_type"];
        if (mapp(details["resource_costs"]))
            payload["resource_costs"] = copy(details["resource_costs"]);
        if (mapp(details["effects"]))
            payload["effects"] = copy(details["effects"]);
        if (arrayp(details["status_effects"])) {
            payload["status_effects"] = details["status_effects"];
            payload["statuses"] = COMBAT_EFFECT_D->build_combat_status_instances(
                details["status_effects"], source_is_viewer ? "target" : "self");
        } else if (event == "status_effect" && (stringp(details["effect_id"]) || stringp(details["effect_name"]))) {
            status = ([
                "id": details["effect_id"] || details["effect_name"] || "status_effect",
                "name": details["effect_name"] || details["effect_id"] || "状态效果",
                "type": details["effect_type"] || "",
            ]);
            statuses = COMBAT_EFFECT_D->build_combat_status_instances(({ status }), source_is_viewer ? "target" : "self");
            if (sizeof(statuses) > 0)
                payload["statuses"] = statuses;
        }
    }
    return payload;
}

void msg_send(object ob, string msg)
{
    call_other(DAEMON_BRIDGE, "msg_send", ob, msg);
}

void combat_msg(object who, string text)
{
    if (!objectp(who) || !stringp(text))
        return;

    call_other(DAEMON_BRIDGE, "msg_combat", who, text, "combat_text");
}

void combat_packet(object who, string type, mapping payload)
{
    if (!objectp(who) || !stringp(type))
        return;
    if (!CLIENT_SYNC_D->can_deliver_combat_packet_to_target(who))
        return;

    call_other(DAEMON_BRIDGE, "send_xk_combat_packet", who, type, payload || ([]));
}

/* 战斗文本由结构化包承载，批量投递保持单帧 XK_BACH，不另发 MSGE。
 * 返回 0＝门控拒收（整批丢弃），1＝已投递。 */
int deliver_combat_batch(object target, mixed *messages)
{
    if (!objectp(target) || !arrayp(messages) || sizeof(messages) == 0)
        return 0;
    if (!CLIENT_SYNC_D->can_deliver_combat_packet_to_target(target))
        return 0;

    call_other(DAEMON_BRIDGE, "send_xk_batch", target, messages);
    return 1;
}

private mixed *query_combat_status_instances(object who, string owner)
{
    mapping buff;
    mixed *effects;
    string buff_id;

    if (!objectp(who) || !function_exists("query_buffs", who))
        return ({ });

    if (function_exists("update_buffs", who))
        who->update_buffs();
    effects = ({ });
    foreach (buff_id, buff in who->query_buffs() || ([ ])) {
        if (mapp(buff) && buff["combat_status"])
            effects += ({ copy(buff) });
    }
    return COMBAT_EFFECT_D->build_combat_status_instances(effects, owner);
}

private object query_combat_opponent(object who)
{
    mapping combat;
    object opponent;
    string combat_id;

    if (!objectp(who))
        return 0;

    combat_id = COMBAT_D->query_combat_id(who);
    combat = stringp(combat_id) && combat_id != "" ? COMBAT_D->query_combat(combat_id) : 0;
    if (mapp(combat)) {
        if (combat["attacker"] == who)
            opponent = combat["defender"];
        else if (combat["defender"] == who)
            opponent = combat["attacker"];
    }
    if (!objectp(opponent))
        opponent = who->query_temp("fight_ob");
    return objectp(opponent) ? opponent : 0;
}

mapping build_combat_runtime_snapshot(object who)
{
    mapping payload;
    object opponent;
    string combat_id;

    if (!objectp(who))
        return ([]);

    combat_id = COMBAT_D->query_combat_id(who);
    opponent = query_combat_opponent(who);
    payload = ([
        "active": COMBAT_D->is_in_combat(who) ? 1 : 0,
        "combat_id": stringp(combat_id) ? combat_id : "",
        "busy_seconds": function_exists("query_busy_left", who) ? who->query_busy_left() : 0,
        "self_statuses": query_combat_status_instances(who, "self"),
        "target_statuses": objectp(opponent) ? query_combat_status_instances(opponent, "target") : ({ }),
        "cooldowns": NUMERIC_RULE_D->query_action_budget_runtime_cooldowns(who),
    ]);
    if (objectp(opponent))
        payload["opponent"] = query_living_name(opponent, "对手");
    return payload;
}

void send_damage_packets(object attacker, object defender, int final_damage,
    int defender_hp, mapping details)
{
    mapping payload;
    string event;
    string source_name;
    string target_name;

    event = mapp(details) && stringp(details["event"]) && details["event"] != ""
        ? details["event"]
        : "damage";
    source_name = query_living_name(attacker, "对手");
    target_name = query_living_name(defender, "目标");
    payload = build_damage_payload(event, source_name, target_name, final_damage,
        defender, defender_hp, details, 1);
    COMBAT_D->combat_packet(attacker, XK_CBDM, payload);
    payload = build_damage_payload(event, source_name, target_name, final_damage,
        defender, defender_hp, details, 0);
    COMBAT_D->combat_packet(defender, XK_CBDM, payload);
}

void send_hit_messages(object attacker, object defender, int final_damage,
    mapping details)
{
    return;
}

void send_flee_end_packets(object who, object opponent)
{
    string who_name;
    string opponent_name;

    who_name = query_living_name(who, "对手");
    opponent_name = query_living_name(opponent, "对手");

    if (objectp(who))
        combat_packet(who, XK_CBED, ([
            "result": "flee",
            "opponent": opponent_name,
            "message": sprintf("你抽身后撤，脱离了与%s的战局。", opponent_name),
            "result_label": "脱离战斗"
        ]));
    if (objectp(opponent))
        combat_packet(opponent, XK_CBED, ([
            "result": "opponent_fled",
            "opponent": who_name,
            "message": sprintf("%s虚晃一招，脱离了战局。", who_name),
            "result_label": "对手脱离"
        ]));
}
