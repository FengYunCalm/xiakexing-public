/**
 * @file    skill_d.c
 * @brief   技能服务真源



 */

#pragma strict_types
#pragma save_types

#include <game/skill.h>
#include <system/services.h>

#undef is_player

inherit "/lib/framework/daemon";

/* L-66：等级经验阈值的单一真源（升级循环与技能详情共用；协议层不得按
 * level 推算第二套进度公式）。 */
#define SKILL_LEVEL_EXP_THRESHOLD 10

// `skill_d.c` owns skill definitions and player skill data. Combat execution
// can consume these values, but should not fork its own skill progression source.
nosave mapping _skill_definitions;
nosave int _skill_config_generation;
/* L-67：目录加载成功与配置读取失败分开——失败时读路径不得把既有技能当作
 * “未注册”处理（内容退役/有效空目录才是真正的未知技能）。 */
nosave int _skill_catalog_load_ok;

int query_skill_config_generation();

private void remember_player_skills_normalized(object player)
{
    int projection_generation;

    if (!objectp(player))
        return;
    projection_generation = function_exists(
        "query_xk_skill_projection_generation", player)
            ? player->query_xk_skill_projection_generation() : 0;
    player->set_temp("skill/normalized_state", ([
        "projection_generation": projection_generation,
        "skill_config_generation": query_skill_config_generation(),
    ]));
}

varargs void invalidate_player_skill_projection(object player, string skill_id)
{
    mapping resource_impact;

    if (!objectp(player))
        return;
    resource_impact = stringp(skill_id) && skill_id != ""
        ? NUMERIC_RULE_D->query_skill_resource_impact(skill_id)
        : ([ "hp": 1, "mp": 1, "ep": 1 ]);
    player->delete_temp("xk/skill_list_cache");
    player->delete_temp("xk/skill_list_threshold_cache");
    if (function_exists("invalidate_xk_skill_projection", player))
        player->invalidate_xk_skill_projection();
    if (mapp(resource_impact)
        && (resource_impact["hp"] || resource_impact["mp"])) {
        if (function_exists("invalidate_xk_vital_maxima_projection", player))
            player->invalidate_xk_vital_maxima_projection(1);
    } else if (mapp(resource_impact) && resource_impact["ep"]
        && function_exists("invalidate_xk_vital_maxima_projection", player)) {
        player->invalidate_xk_vital_maxima_projection(0);
    }
    if (function_exists("invalidate_xk_score_projection", player))
        player->invalidate_xk_score_projection(1);
    if (function_exists("schedule_owner_score_read_model_refresh", player))
        player->schedule_owner_score_read_model_refresh();
    remember_player_skills_normalized(player);
}

private int commit_player_skills(object player, mapping skills,
    string operation)
{
    mapping change;
    mapping response;

    if (!objectp(player) || !mapp(skills))
        return 0;
    if (!function_exists("is_player", player) || !player->is_player())
        return player->set("skills", skills);
    change = ([ "skills": skills ]);
    response = PLAYER_STATE_D->commit_state_fields(player, change,
        PLAYER_STATE_D->build_state_idempotency_key(player,
            operation || "skill/player_skills", change));
    return mapp(response) && response["ok"];
}

private mapping query_skill_config()
{
    mapping config;

    config = CONFIG_D->query_full_config("skill_catalog");
    return mapp(config) ? config : ([]);
}
private void load_skill_config()
{
    mapping config;
    mapping skills;

    config = query_skill_config();
    if (!mapp(config["skills"])) {
        /* L-67：配置缺失/读取失败与有效空目录必须分开——失败时保留已知
         * 定义并标记目录不可信，读路径据此不把全部技能判为未注册。 */
        _skill_catalog_load_ok = 0;
        if (!mapp(_skill_definitions))
            _skill_definitions = ([]);
        _skill_config_generation++;
        log_file("system/skill", sprintf(
            "[%s] skill catalog load failed; existing player skills untouched\n",
            ctime(time())));
        return;
    }
    skills = copy(config["skills"]);
    _skill_definitions = skills;
    _skill_catalog_load_ok = 1;
    _skill_config_generation++;
}

void on_config_reloaded(string key, string config_name)
{
    if (key != "skill_catalog" && config_name != "skill_catalog")
        return;

    load_skill_config();
}

void on_config_reload_rollback(string key, string config_name)
{
    if (key != "skill_catalog" && config_name != "skill_catalog")
        return;

    load_skill_config();
    if (_skill_config_generation > 1)
        _skill_config_generation -= 2;
    if (_skill_config_generation < 1)
        _skill_config_generation = 1;
}

private mapping normalize_skill_record(mixed raw_data)
{
    int level;
    int exp;

    if (mapp(raw_data)) {
        level = intp(raw_data["level"]) ? raw_data["level"] : 0;
        exp = intp(raw_data["exp"]) ? raw_data["exp"] : 0;

        if (level < 0)
            level = 0;
        if (level > SKILL_MAX_LEVEL)
            level = SKILL_MAX_LEVEL;
        if (exp < 0)
            exp = 0;

        return ([ "level": level, "exp": exp ]);
    }

    if (intp(raw_data)) {
        level = raw_data;
        if (level < 0)
            level = 0;
        if (level > SKILL_MAX_LEVEL)
            level = SKILL_MAX_LEVEL;

        return ([ "level": level, "exp": 0 ]);
    }

    return 0;
}

void create()
{
    ::create();
    if (!mapp(_skill_definitions))
        _skill_definitions = ([]);

    load_skill_config();
    CONFIG_D->compat_register_config_callback("skill_catalog", "on_config_reloaded", this_object());
}

mapping get_skill_definition(string skill_id)
{
    if (!stringp(skill_id) || !mapp(_skill_definitions))
        return 0;
    return copy(_skill_definitions[skill_id]);
}

int query_skill_config_generation()
{
    return _skill_config_generation > 0 ? _skill_config_generation : 1;
}

/* L-66：升级所需经验阈值（详情/训练同源读取）。 */
int query_skill_level_exp_threshold()
{
    return SKILL_LEVEL_EXP_THRESHOLD;
}

/* L-153/L-67：技能目录是否可用（加载成功且非读取失败）。校验方据此区分
 * “目录为空”与“目录不可信”，不可信时不得据此剔除来源或隐藏技能。 */
int is_skill_catalog_trusted()
{
    return _skill_catalog_load_ok ? 1 : 0;
}

string *get_all_skill_ids()
{
    return keys(_skill_definitions || ([]));
}

int skill_exists(string skill_id)
{
    return mapp(get_skill_definition(skill_id));
}

/* L-67/L-152：读取路径只产出视图——不回写、不删除玩家持久技能。
 * 定义缺失的技能保留原始 level/exp 并标记 missing_definition（列表隐藏、
 * 详情可报缺失，定义恢复后等级原样可用）；形态归一化（clamp）也只在视图内，
 * 不成为未经授权的数据迁移。显式清理另行授权，不在登录/详情/预热读取中做。 */
private mapping build_player_skill_view(mapping skills)
{
    mapping view;
    mapping normalized;
    mixed raw;
    string skill_id;

    view = ([]);
    if (!mapp(skills))
        return view;
    if (!mapp(_skill_definitions))
        return copy(skills);
    foreach (skill_id in keys(skills)) {
        if (!stringp(skill_id) || skill_id == "")
            continue;
        if (!mapp(_skill_definitions[skill_id])) {
            /* 定义缺失：保留持久 level/exp，仅标记（列表隐藏、详情报缺失）。 */
            view[skill_id] = mapp(skills[skill_id])
                ? skills[skill_id] + ([ "missing_definition": 1 ])
                : skills[skill_id];
            continue;
        }
        raw = skills[skill_id];
        /* 已是规范形态（int level/exp、范围内、无附加键）时零分配复用。 */
        if (mapp(raw) && sizeof(raw) == 2 && intp(raw["level"]) && intp(raw["exp"])
            && raw["level"] >= 0 && raw["level"] <= SKILL_MAX_LEVEL
            && raw["exp"] >= 0) {
            view[skill_id] = raw;
            continue;
        }
        normalized = normalize_skill_record(raw);
        view[skill_id] = mapp(normalized) ? normalized : raw;
    }
    return view;
}

/* 显式写入路径使用的原始技能表（不携带视图标记）。 */
private mapping query_raw_player_skills(object player)
{
    mapping skills;

    if (!objectp(player))
        return 0;
    skills = player->query("skills");
    return mapp(skills) ? copy(skills) : ([]);
}

private mapping ensure_player_skills(object player)
{
    mapping skills;

    if (!objectp(player))
        return 0;
    skills = player->query("skills");
    if (!mapp(skills))
        return ([]);
    /* 目录不可信（配置读取失败）时原样返回：不标记、不隐藏。 */
    if (!_skill_catalog_load_ok)
        return copy(skills);
    return build_player_skill_view(skills);
}

/* L-67：定义缺失但仍有持久数据的技能清单（数据清点入口，供显式迁移/
 * 授权清理与诊断使用，不参与读路径）。 */
string *query_player_unknown_skill_ids(object player)
{
    mapping skills;
    string *unknown;
    string skill_id;

    skills = query_raw_player_skills(player);
    unknown = ({ });
    if (!mapp(skills))
        return unknown;
    foreach (skill_id in keys(skills)) {
        if (stringp(skill_id) && skill_id != "" && !skill_exists(skill_id))
            unknown += ({ skill_id });
    }
    return unknown;
}

mapping get_player_skill_data(object player, string skill_id)
{
    mapping skills;

    skills = ensure_player_skills(player);
    if (!mapp(skills) || !stringp(skill_id))
        return 0;

    return skills[skill_id];
}

mapping query_player_skills(object player)
{
    mapping view;
    mapping visible;
    string skill_id;

    view = ensure_player_skills(player);
    if (!mapp(view))
        return ([]);

    /* L-67：定义缺失的技能不下发（列表隐藏），持久数据仍在玩家存档中，
     * 定义恢复后等级/经验原样回归。 */
    visible = ([]);
    foreach (skill_id in keys(view)) {
        if (mapp(view[skill_id]) && view[skill_id]["missing_definition"])
            continue;
        visible[skill_id] = view[skill_id];
    }
    return visible;
}

int query_player_skill_level(object player, string skill_id)
{
    mapping skill_data;

    skill_data = get_player_skill_data(player, skill_id);
    if (!mapp(skill_data))
        return 0;

    return intp(skill_data["level"]) ? skill_data["level"] : 0;
}

int learn_skill(object player, string skill_id)
{
    mapping skills;
    mapping def;
    int cap;

    if (!objectp(player) || !stringp(skill_id))
        return 0;

    def = get_skill_definition(skill_id);
    if (!mapp(def))
        return 0;
    cap = NUMERIC_RULE_D->query_skill_training_cap(player, skill_id);
    if (cap < 1)
        return 0;

    /* L-67：写入路径使用原始技能表（不携带读视图标记），提交即唯一写入。 */
    skills = query_raw_player_skills(player);
    if (!mapp(skills))
        return 0;
    if (!mapp(skills[skill_id]))
        skills[skill_id] = ([ "level": 1, "exp": 0 ]);
    if (!commit_player_skills(player, skills, "skill/learn"))
        return 0;
    invalidate_player_skill_projection(player, skill_id);
    return 1;
}

private void notify_breakthrough_required(object player, string skill_id, mapping gate)
{
    string text;
    mixed emitted;
    mixed err;

    if (!objectp(player) || !mapp(gate) || !gate["blocked"])
        return;
    text = sprintf("「%s」已到达第%d个突破节点，请先完成突破选择后再继续提升。",
        skill_id, gate["tier"] || 0);
    err = catch(emitted = MESSAGE_D->emit_message_event(player, text, ([
        "channel": "system",
        "intent": "notify",
        "priority": "normal",
        "reliability": "important",
        "display_mode": "float",
        "ttl_ms": 3500,
        "collapse_key": "float/skill_breakthrough/" + skill_id,
        "payload": ([
            "kind": "skill_breakthrough_required",
            "skill_id": skill_id,
            "tier": gate["tier"] || 0,
            "required_level": gate["required_level"] || 0,
        ]),
    ])));
    if (err || !emitted)
        msg_send(player, text + "\n");
}

int improve_skill(object player, string skill_id, int exp)
{
    mapping gate;
    mapping skills;
    mapping data;
    int cap;
    int upgrade_cap;
    int changed;
    int current_level;

    if (!objectp(player) || !stringp(skill_id) || exp <= 0)
        return 0;

    /* L-67：写入路径使用原始技能表（不携带读视图标记）。 */
    skills = query_raw_player_skills(player);
    if (!mapp(skills))
        return 0;
    data = skills[skill_id];
    if (!mapp(data))
        data = normalize_skill_record(data);
    if (!mapp(data))
        return 0;
    current_level = data["level"] || 0;
    gate = SKILL_BREAKTHROUGH_D->query_upgrade_gate(player, skill_id);
    if (mapp(gate) && gate["blocked"]) {
        notify_breakthrough_required(player, skill_id, gate);
        return 0;
    }
    cap = NUMERIC_RULE_D->query_skill_training_cap(player, skill_id);
    upgrade_cap = cap;
    if (mapp(gate) && gate["required_level"] > current_level
        && gate["required_level"] < upgrade_cap)
        upgrade_cap = gate["required_level"];
    if (current_level >= upgrade_cap) {
        if (mapp(gate) && gate["required_level"] == current_level)
            notify_breakthrough_required(player, skill_id, gate);
        return 0;
    }

    data["exp"] = (data["exp"] || 0) + exp;
    changed = 1;
    while ((data["exp"] || 0) >= SKILL_LEVEL_EXP_THRESHOLD
        && (data["level"] || 0) < upgrade_cap) {
        data["exp"] -= SKILL_LEVEL_EXP_THRESHOLD;
        data["level"] = (data["level"] || 0) + 1;
        changed = 1;
    }
    /* 只有真正达到训练上限才清除余量；突破节点的余量须保留，
     * 选择词条后可以继续消费。 */
    if ((data["level"] || 0) >= cap)
        data["exp"] = 0;
    skills[skill_id] = data;
    if (!commit_player_skills(player, skills, "skill/improve"))
        return 0;
    invalidate_player_skill_projection(player, skill_id);
    if ((data["level"] || 0) >= upgrade_cap && upgrade_cap < cap) {
        gate = SKILL_BREAKTHROUGH_D->query_upgrade_gate(player, skill_id);
        notify_breakthrough_required(player, skill_id, gate);
    }
    return changed;
}

int query_player_skill_cap(object player, string skill_id)
{
    return NUMERIC_RULE_D->query_skill_training_cap(player, skill_id);
}

mapping query_player_skill_train_status(object player, string skill_id)
{
    return NUMERIC_RULE_D->query_skill_train_status(player, skill_id);
}

int use_skill(object player, string skill_id, object target)
{
    mapping data;

    if (!objectp(player) || !stringp(skill_id))
        return 0;
    data = get_player_skill_data(player, skill_id);
    /* L-67：定义缺失的技能不可用（持久数据保留，但不作为可用技能下发）。 */
    return mapp(data) && !data["missing_definition"];
}

int calculate_skill_power(object player, string skill_id)
{
    mapping data;

    data = get_player_skill_data(player, skill_id);
    if (!mapp(data))
        return 0;
    return NUMERIC_RULE_D->query_effective_skill_level(player, skill_id) * 2;
}

/* ===== 领域协议发送器（自 kernel/simul_efun.c 内联实现迁入，逐字保留） ===== */

/* 发射路径契约：保持既有 CLIENT_SYNC_D 事务入队语义（tx_add_skill_event +
 * submit_transaction），禁止改为直发 xk_protocol_core。 */
void xk_send_skill_event(object me, mapping payload)
{
    mapping tx;

    if (!objectp(me))
        return;

    tx = CLIENT_SYNC_D->tx_add_skill_event(0,
        mapp(payload) ? payload : ([]));
    tx = CLIENT_SYNC_D->tx_with_response_context(tx, 0);
    CLIENT_SYNC_D->submit_transaction(me, tx);
}
