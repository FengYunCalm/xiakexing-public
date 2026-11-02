/**
 * @file    buff_module.c
 * @brief   Buff/Debuff 模块



 *
 * @par Description:
 * 基于原 `F_CONDITION.c`，负责 Buff/Debuff 的添加、移除、更新和叠加规则。
 *
 * @note
 * 本模块仍是 legacy-but-canonical true source。
 * Buff/Condition 语义更接近对象能力模块，因此保留在 /daemons/modules/buff。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/constants.h>
#include <system/errors.h>
#include <framework/message_facade.h>

mixed query_temp(string path);
int set_temp(string path, mixed value);

/* Buff类型 */
#define BUFF_TYPE_BUFF          "buff"      // 增益
#define BUFF_TYPE_DEBUFF        "debuff"    // 减益
#define BUFF_TYPE_NEUTRAL       "neutral"   // 中性
#define BUFF_TYPE_POISON        "poison"    // 中毒
#define BUFF_TYPE_HEAL          "heal"      // 疗伤

/* 叠加规则 */
#define STACK_RULE_REFRESH      "refresh"   // 刷新时间
#define STACK_RULE_STACK        "stack"     // 叠加层数
#define STACK_RULE_REPLACE      "replace"   // 替换效果
#define STACK_RULE_IGNORE       "ignore"    // 忽略新效果

/* Buff效果相关常量 */
#define BUFF_ID_MAX_LEN         64          // Buff ID最大长度
#define HASTE_SPEED_BONUS       20          // 加速效果值
#define SLOW_SPEED_PENALTY      -20         // 减速效果值

/**
 * @brief 获取所有Buff
 * @return Buff数据映射
 */
mapping query_buffs()
{
    mapping buffs = query_temp("buffs");
    return mapp(buffs) ? buffs : ([]);
}

/**
 * @brief 获取单个Buff
 * @param buff_id BuffID
 * @return Buff数据
 */
mapping query_buff(string buff_id)
{
    mapping buffs = query_buffs();
    return buffs[buff_id];
}

private string query_buff_display_name(string buff_id, mapping buff)
{
    string type;

    if (mapp(buff) && stringp(buff["name"]) && buff["name"] != "")
        return buff["name"];

    if (buff_id == "poison")
        return "毒劲";
    if (buff_id == "healing")
        return "疗伤";
    if (buff_id == "strength_up")
        return "力量提升";
    if (buff_id == "armor_up")
        return "护甲提升";
    if (buff_id == "haste")
        return "身法加速";
    if (buff_id == "slow")
        return "身法迟滞";

    type = mapp(buff) ? (buff["type"] || "") : "";
    if (type == BUFF_TYPE_BUFF)
        return "增益状态";
    if (type == BUFF_TYPE_POISON)
        return "毒劲";
    if (type == BUFF_TYPE_HEAL)
        return "疗伤";
    if (type == "control")
        return "压制状态";
    if (type == "dot")
        return "持续伤势";
    return "减益状态";
}

mapping query_buff_runtime_snapshot()
{
    mapping buffs;
    mapping buff;
    mapping snapshot;
    string buff_id;
    int remaining;
    int now;

    buffs = query_buffs();
    snapshot = ([]);
    now = time();

    foreach (buff_id, buff in buffs) {
        if (!mapp(buff))
            continue;
        remaining = 0;
        if (intp(buff["duration"]) && intp(buff["start_time"])) {
            remaining = buff["duration"] - (now - buff["start_time"]);
            if (remaining < 0)
                remaining = 0;
        }
        snapshot[buff_id] = ([
            "name": query_buff_display_name(buff_id, buff),
            "type": buff["type"] || "",
            "stacks": buff["stacks"] || 1,
            "duration": buff["duration"] || 0,
            "remaining": remaining,
        ]);
    }

    return snapshot;
}

/* 前向声明 */
private int _validate_buff_data(string buff_id, mapping data);
private mapping copy_buff_data(mapping data);
private mapping _apply_stack_rule(mapping existing, mapping data);
private void _activate_buff(string buff_id, mapping data, int repeated_application,
    mapping previous_effects);
private void _apply_buff_effects(string buff_id, mapping buff);
private void remove_buff_effects(string buff_id, mapping buff);
private int _suppress_lifecycle_message(mapping buff);
private void send_suppressed_expire_summary(string *buff_names);
private void add_temp_delta(string key, int delta);
private void invalidate_derived_combat_projection(mapping effects, mapping previous_effects);

private void invalidate_derived_combat_projection(mapping effects, mapping previous_effects)
{
    mapping previous_impact;
    mapping resource_impact;
    int affects_hp_dialog;
    int affects_vital_maxima;

    resource_impact = NUMERIC_RULE_D->query_combat_resource_impact(effects);
    previous_impact = NUMERIC_RULE_D->query_combat_resource_impact(previous_effects);
    affects_hp_dialog = (mapp(resource_impact)
            && (resource_impact["hp"] || resource_impact["mp"]))
        || (mapp(previous_impact)
            && (previous_impact["hp"] || previous_impact["mp"]));
    affects_vital_maxima = affects_hp_dialog
        || (mapp(resource_impact) && resource_impact["ep"])
        || (mapp(previous_impact) && previous_impact["ep"]);
    if (affects_hp_dialog) {
        if (function_exists("invalidate_xk_vital_maxima_projection", this_object()))
            this_object()->invalidate_xk_vital_maxima_projection(1);
    } else if (affects_vital_maxima
        && function_exists("invalidate_xk_vital_maxima_projection", this_object())) {
        this_object()->invalidate_xk_vital_maxima_projection(0);
    }
    if (function_exists("invalidate_xk_score_projection", this_object()))
        this_object()->invalidate_xk_score_projection(1);
    if (function_exists("schedule_owner_score_read_model_refresh", this_object()))
        this_object()->schedule_owner_score_read_model_refresh();
}

private void add_temp_delta(string key, int delta)
{
    int current;

    current = query_temp(key) || 0;
    set_temp(key, current + delta);
}

int add_buff(string buff_id, mapping data)
{
    mapping buffs = query_buffs();
    mapping existing;

    if (_validate_buff_data(buff_id, data) != ERR_OK)
        return ERR_INVALID_PARAM;
    data = copy_buff_data(data);
    if (!mapp(data))
        return ERR_INVALID_PARAM;

    existing = buffs[buff_id];

    if (mapp(existing)) {
        data = _apply_stack_rule(existing, data);
        if (!mapp(data))
            return ERR_OK;
        remove_buff_effects(buff_id, existing);
    }

    _activate_buff(buff_id, data, mapp(existing),
        mapp(existing) ? existing["effects"] : 0);
    return ERR_OK;
}

private int _validate_buff_data(string buff_id, mapping data)
{
    mapping effects;
    string attr;
    mixed value;

    if (!stringp(buff_id) || buff_id == "")
        return ERR_INVALID_PARAM;

    if (strlen(buff_id) > 64)
        return ERR_INVALID_PARAM;

    if (!regexp(buff_id, "^[a-zA-Z0-9_]+$"))
        return ERR_INVALID_PARAM;

    if (!mapp(data) || !mapp(data["effects"]))
        return ERR_INVALID_PARAM;

    effects = data["effects"];
    foreach (attr, value in effects) {
        if (!stringp(attr) || !intp(value))
            return ERR_INVALID_PARAM;
    }

    return ERR_OK;
}

private mapping copy_buff_data(mapping data)
{
    mixed copied;

    if (!mapp(data) || catch(copied = restore_variable(save_variable(data))))
        return 0;
    return mapp(copied) ? copied : 0;
}

private mapping _apply_stack_rule(mapping existing, mapping data)
{
    string stack_rule = data["stack_rule"] || STACK_RULE_REFRESH;

    switch (stack_rule) {
        case STACK_RULE_IGNORE:
            return 0;

        case STACK_RULE_REPLACE:
            break;

        case STACK_RULE_STACK:
            data["stacks"] = (existing["stacks"] || 1) + 1;
            if (data["max_stacks"] && data["stacks"] > data["max_stacks"])
                data["stacks"] = data["max_stacks"];
            break;

        case STACK_RULE_REFRESH:
        default:
            break;
    }

    return data;
}

private void _activate_buff(string buff_id, mapping data, int repeated_application,
    mapping previous_effects)
{
    mapping buffs = query_buffs();
    object owner;
    string buff_name;

    data["start_time"] = time();
    if (!data["stacks"])
        data["stacks"] = 1;

    buffs[buff_id] = data;
    set_temp("buffs", buffs);

    _apply_buff_effects(buff_id, data);
    invalidate_derived_combat_projection(data["effects"], previous_effects);

    owner = this_object();
    if (objectp(owner) && !repeated_application && !_suppress_lifecycle_message(data)) {
        buff_name = query_buff_display_name(buff_id, data);
        msg_send(owner, "你获得了「" + buff_name + "」效果。\n");
    }
}

/**
 * @brief 移除Buff
 * @param buff_id BuffID
 * @return 1=成功, 0=失败
 */
int remove_buff(string buff_id)
{
    mapping buffs = query_buffs();
    mapping buff;
    object owner;
    string buff_name;

    if (!mapp(buffs[buff_id]))
        return 0;

    buff = buffs[buff_id];

    remove_buff_effects(buff_id, buff);

    map_delete(buffs, buff_id);
    set_temp("buffs", buffs);
    invalidate_derived_combat_projection(buff["effects"], 0);

    owner = this_object();
    if (objectp(owner) && !_suppress_lifecycle_message(buff)) {
        buff_name = query_buff_display_name(buff_id, buff);
        msg_send(owner, "你的「" + buff_name + "」效果消失了。\n");
    }

    return 1;
}

private int _suppress_lifecycle_message(mapping buff)
{
    return mapp(buff) && buff["suppress_lifecycle_message"] ? 1 : 0;
}

/**
 * @brief 检查是否有Buff
 * @param buff_id BuffID
 * @return 1=有, 0=无
 */
int has_buff(string buff_id)
{
    return mapp(query_buff(buff_id));
}

/**
 * @brief 更新Buff（检查过期等）
 */
void update_buffs()
{
    mapping buffs = query_buffs();
    string *buff_ids = keys(buffs);
    string *suppressed_expired;
    string buff_id;
    mapping buff;
    int now = time();

    suppressed_expired = ({ });
    foreach (buff_id in buff_ids) {
        buff = buffs[buff_id];
        if (!mapp(buff) || buff["round_driven_lifecycle"])
            continue;

        // 检查是否过期
        if (buff["duration"] > 0) {
            if (now - buff["start_time"] > buff["duration"]) {
                if (_suppress_lifecycle_message(buff))
                    suppressed_expired += ({ query_buff_display_name(buff_id, buff) });
                remove_buff(buff_id);
            }
        }
    }
    send_suppressed_expire_summary(suppressed_expired);
}

private void send_suppressed_expire_summary(string *buff_names)
{
    if (!arrayp(buff_names) || sizeof(buff_names) == 0)
        return;
    if (sizeof(buff_names) == 1) {
        msg_send(this_object(), "你的「" + buff_names[0] + "」效果消失了。\n");
        return;
    }
    msg_send(this_object(), sprintf("你的%d个状态效果消失了：%s。\n",
        sizeof(buff_names), implode(buff_names, "、")));
}

/**
 * @brief 清除所有Buff
 */
void clear_all_buffs()
{
    mapping buffs = query_buffs();
    string *buff_ids = keys(buffs);

    foreach (string buff_id in buff_ids) {
        remove_buff(buff_id);
    }
}

/**
 * @brief 应用Buff效果
 */
private void _apply_buff_effects(string buff_id, mapping buff)
{
    mapping effects = buff["effects"];
    string attr;
    int stacks;
    mixed value;

    if (!mapp(effects))
        return;

    stacks = buff["stacks"] || 1;
    if (stacks < 1)
        stacks = 1;
    foreach (attr, value in effects) {
        if (!stringp(attr) || !intp(value))
            continue;
        // 将效果应用到apply_temp
        add_temp_delta("apply/" + attr, value * stacks);
        add_temp_delta("buff/" + attr, value * stacks);
    }
}

/**
 * @brief 移除Buff效果
 */
private void remove_buff_effects(string buff_id, mapping buff)
{
    mapping effects = buff["effects"];
    string attr;
    int stacks;
    mixed value;

    if (!mapp(effects))
        return;

    stacks = buff["stacks"] || 1;
    if (stacks < 1)
        stacks = 1;
    foreach (attr, value in effects) {
        if (!stringp(attr) || !intp(value))
            continue;
        add_temp_delta("apply/" + attr, -value * stacks);
        add_temp_delta("buff/" + attr, -value * stacks);
    }
}

/**
 * @brief 添加中毒状态（纯时长 debuff，无 tick 伤害）
 * @param duration 持续时间（秒）
 */
int apply_poison(int duration)
{
    if (duration <= 0)
        return ERR_INVALID_PARAM;

    return add_buff("poison", ([
        "type": BUFF_TYPE_POISON,
        "duration": duration,
        "stack_rule": STACK_RULE_REFRESH,
        "effects": ([]),
    ]));
}

/**
 * @brief 添加疗伤状态（纯时长 buff，无 tick 治疗）
 * @param duration 持续时间（秒）
 */
int apply_healing(int duration)
{
    if (duration <= 0)
        return ERR_INVALID_PARAM;

    return add_buff("healing", ([
        "type": BUFF_TYPE_HEAL,
        "duration": duration,
        "stack_rule": STACK_RULE_REFRESH,
        "effects": ([]),
    ]));
}

/**
 * @brief 清除所有中毒状态原 condition_generic 迁移，living 直接持有）
 */
void cure_poison()
{
    mapping buffs;
    string buff_id;

    buffs = query_buffs();
    foreach (buff_id in keys(buffs)) {
        if (buffs[buff_id]["type"] == BUFF_TYPE_POISON || buff_id == "poison")
            remove_buff(buff_id);
    }
}

/**
 * @brief 清除所有减益状态（含中毒）
 */
void cure_debuff()
{
    mapping buffs;
    string buff_id;

    buffs = query_buffs();
    foreach (buff_id in keys(buffs)) {
        if (buffs[buff_id]["type"] == BUFF_TYPE_DEBUFF ||
            buffs[buff_id]["type"] == BUFF_TYPE_POISON)
            remove_buff(buff_id);
    }
}

/**
 * @brief 添加力量加成
 * @param amount 加成值
 * @param duration 持续时间
 */
void apply_strength_buff(int amount, int duration)
{
    add_buff("strength_up", ([
        "type": BUFF_TYPE_BUFF,
        "duration": duration,
        "stack_rule": STACK_RULE_REFRESH,
        "effects": ([ "str": amount ])
    ]));
}

/**
 * @brief 添加防御加成
 * @param amount 加成值
 * @param duration 持续时间
 */
void apply_armor_buff(int amount, int duration)
{
    add_buff("armor_up", ([
        "type": BUFF_TYPE_BUFF,
        "duration": duration,
        "stack_rule": STACK_RULE_REFRESH,
        "effects": ([ "armor": amount ])
    ]));
}

/**
 * @brief 添加加速效果
 * @param duration 持续时间
 */
void apply_haste(int duration)
{
    add_buff("haste", ([
        "type": BUFF_TYPE_BUFF,
        "duration": duration,
        "stack_rule": STACK_RULE_REFRESH,
        "effects": ([ "speed": HASTE_SPEED_BONUS ])
    ]));
}

/**
 * @brief 添加减速效果
 * @param duration 持续时间
 */
void apply_slow(int duration)
{
    add_buff("slow", ([
        "type": BUFF_TYPE_DEBUFF,
        "duration": duration,
        "stack_rule": STACK_RULE_REFRESH,
        "effects": ([ "speed": SLOW_SPEED_PENALTY ])
    ]));
}

/**
 * @brief 显示当前Buff列表
 */
void show_buffs()
{
    mapping buffs = query_buffs();
    string *buff_ids;
    string buff_id;
    mapping buff;
    string output;
    int now = time();
    int remaining;
    string type_name;

    if (!mapp(buffs) || sizeof(buffs) == 0) {
        msg_send(this_object(), "你目前没有特殊状态。\n");
        return;
    }

    output = "【状态效果】\n";
    buff_ids = keys(buffs);

    foreach (buff_id in buff_ids) {
        buff = buffs[buff_id];
        remaining = buff["duration"] - (now - buff["start_time"]);

        if (buff["type"] == BUFF_TYPE_POISON)
            type_name = "中毒";
        else if (buff["type"] == BUFF_TYPE_HEAL)
            type_name = "疗伤";
        else if (buff["type"] == BUFF_TYPE_BUFF)
            type_name = "增益";
        else
            type_name = "减益";

        output += sprintf("  %s: %s (%d秒) x%d\n",
            type_name,
            query_buff_display_name(buff_id, buff),
            remaining > 0 ? remaining : 0,
            buff["stacks"] || 1
        );
    }

    msg_send(this_object(), output);
}

/**
 * @brief 获取Buff层数
 * @param buff_id Buff ID
 * @return int Buff 层数
 */
int query_buff_stacks(string buff_id)
{
    mapping buff = query_buff(buff_id);
    if (!mapp(buff))
        return 0;
    return buff["stacks"] || 0;
}
