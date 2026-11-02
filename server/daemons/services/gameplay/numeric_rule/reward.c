/**
 * @file    numeric_rule/reward.c
 * @brief   奖励包与奖励递减
 *
 * @par Description:
 * 含 {query_pve_budget_framework, query_equipment_budget_framework, query_equipment_affix_framework, query_npc_combat_template_framework, query_npc_combat_skill_framework, query_reward_diminishing_framework, query_reward_package_budget_framework, query_action_budget_framework, query_reward_package_source_config, query_reward_package_source, query_reward_package_allowed_fields, reward_package_allows_numeric_field, query_reward_package_attachment_count, query_reward_package_int, query_reward_package_budget_score, query_reward_package_budget_report, query_reward_diminishing_source_config, query_reward_diminishing_source, query_reward_diminishing_target_key, query_reward_diminishing_tracked_fields, query_reward_diminishing_int, query_tracked_reward_amount, query_reward_diminishing_store, query_reward_diminishing_state_key, query_reward_diminishing_state, scale_reward_diminishing_rewards, query_reward_diminishing_result, record_reward_diminishing_event, clear_reward_diminishing_state}。
 *
 * 本模块是 reward 域纯规则片段，由 `numeric_rule_d.c` 继承；
 * 不持有 daemon 状态（nosave 全部留在 facade），
 * 配置经 facade 的 query_section/query_section_int 读取。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <game/skill.h>
#include <game_constants.h>
#include "defs.h"
int query_mapping_int(mapping source, string key, int default_value);
mapping query_section(string section_name);
mapping query_reward_diminishing_state(object player, string reward_source, string target_key);
mapping query_pve_budget_framework()
{
    mapping framework;

    framework = query_section("pve_budget");
    return mapp(framework) ? copy(framework) : ([]);
}
mapping query_equipment_budget_framework()
{
    mapping framework;

    framework = query_section("equipment_budget");
    return mapp(framework) ? copy(framework) : ([]);
}
mapping query_equipment_affix_framework()
{
    mapping framework;
    mapping affix;

    framework = query_equipment_budget_framework();
    affix = framework["affix"];
    return mapp(affix) ? copy(affix) : ([]);
}
mapping query_npc_combat_template_framework()
{
    mapping framework;

    framework = query_section("npc_combat_template");
    return mapp(framework) ? copy(framework) : ([]);
}
mapping query_npc_combat_skill_framework()
{
    mapping framework;

    framework = query_section("npc_combat_skill");
    return mapp(framework) ? copy(framework) : ([]);
}
mapping query_reward_diminishing_framework()
{
    mapping framework;

    framework = query_section("reward_diminishing");
    return mapp(framework) ? copy(framework) : ([]);
}
mapping query_reward_package_budget_framework()
{
    mapping framework;

    framework = query_section("reward_package_budget");
    return mapp(framework) ? copy(framework) : ([]);
}
mapping query_action_budget_framework()
{
    mapping framework;

    framework = query_section("action_budget");
    return mapp(framework) ? copy(framework) : ([]);
}
mapping query_reward_package_source_config(string reward_source)
{
    mapping framework;
    mapping source_config;
    mapping sources;
    string prefix;
    int colon;

    if (!stringp(reward_source) || reward_source == "")
        reward_source = "reward_apply";

    framework = query_reward_package_budget_framework();
    sources = mapp(framework["sources"]) ? framework["sources"] : ([]);
    source_config = sources[reward_source];
    if (mapp(source_config))
        return copy(source_config);

    colon = strsrch(reward_source, ":");
    if (colon > 0) {
        prefix = reward_source[0..colon - 1];
        source_config = sources[prefix];
    }
    return mapp(source_config) ? copy(source_config) : ([]);
}
private string query_reward_package_source(mapping options)
{
    string source;

    source = mapp(options) ? options["source"] : "";
    return stringp(source) && source != "" ? source : "reward_apply";
}
private mixed *query_reward_package_allowed_fields(mapping source_config, mapping defaults)
{
    mixed *fields;

    fields = mapp(source_config) ? source_config["allowed_numeric_fields"] : 0;
    if (!arrayp(fields) || sizeof(fields) == 0)
        fields = mapp(defaults) ? defaults["allowed_numeric_fields"] : 0;
    return arrayp(fields) && sizeof(fields) > 0
        ? fields
        : ({ "gold", "potential", "exp", "sect_contribution" });
}
private int reward_package_allows_numeric_field(string field, mixed *allowed_fields)
{
    if (!stringp(field) || field == "")
        return 0;
    if (!arrayp(allowed_fields))
        return 0;
    if (member_array("*", allowed_fields) != -1)
        return 1;
    return member_array(field, allowed_fields) != -1;
}
private int query_reward_package_attachment_count(mapping rewards)
{
    mixed attachment;
    mixed *attachments;
    int count;
    int item_count;

    if (!mapp(rewards))
        return 0;

    attachments = arrayp(rewards["attachments"]) ? rewards["attachments"] : ({ });
    foreach (attachment in attachments) {
        if (mapp(attachment) && intp(attachment["count"]) && attachment["count"] > 0)
            count += attachment["count"];
        else
            count += 1;
    }

    if (stringp(rewards["item"]) && rewards["item"] != "") {
        item_count = intp(rewards["item_count"]) && rewards["item_count"] > 0
            ? rewards["item_count"]
            : 1;
        count += item_count;
    }
    return count;
}
private int query_reward_package_int(mapping source_config, mapping defaults,
    string key, int default_value)
{
    if (mapp(source_config) && !undefinedp(source_config[key]) && intp(source_config[key]))
        return source_config[key];
    if (mapp(defaults) && !undefinedp(defaults[key]) && intp(defaults[key]))
        return defaults[key];
    return default_value;
}
int query_reward_package_budget_score(mapping rewards)
{
    mapping extra_scores;
    mapping framework;
    mapping weights;
    string field;
    int amount;
    int attachment_count;
    int score;

    if (!mapp(rewards))
        return 0;

    framework = query_reward_package_budget_framework();
    weights = mapp(framework["field_weights"]) ? framework["field_weights"] : ([]);
    foreach (field, amount in weights) {
        if (!stringp(field))
            continue;
        if (intp(rewards[field]) && rewards[field] > 0)
            score += rewards[field] * amount;
    }

    extra_scores = mapp(framework["extra_scores"]) ? framework["extra_scores"] : ([]);
    attachment_count = query_reward_package_attachment_count(rewards);
    if (attachment_count > 0)
        score += attachment_count * query_mapping_int(extra_scores, "attachment", 200);
    if (stringp(rewards["title"]) && rewards["title"] != "")
        score += query_mapping_int(extra_scores, "title", 1000);

    return score;
}
mapping query_reward_package_budget_report(mapping rewards, mapping options)
{
    mapping breakdown;
    mapping defaults;
    mapping extra_scores;
    mapping framework;
    mapping source_config;
    mapping weights;
    mixed *allowed_fields;
    mixed *violations;
    mixed value;
    string field;
    string reward_source;
    int allow_attachments;
    int allow_title;
    int amount;
    int attachment_count;
    int block_apply;
    int block_disallowed;
    int block_over_budget;
    int constraint_violations;
    int limit;
    int score;
    int source_known;

    if (!mapp(rewards))
        return ([ "ok": 0, "error": "invalid_rewards" ]);

    framework = query_reward_package_budget_framework();
    defaults = mapp(framework["defaults"]) ? framework["defaults"] : ([]);
    weights = mapp(framework["field_weights"]) ? framework["field_weights"] : ([]);
    extra_scores = mapp(framework["extra_scores"]) ? framework["extra_scores"] : ([]);
    reward_source = query_reward_package_source(options);
    source_config = query_reward_package_source_config(reward_source);
    source_known = mapp(source_config) && sizeof(keys(source_config)) > 0;
    allowed_fields = query_reward_package_allowed_fields(source_config, defaults);
    limit = query_reward_package_int(source_config, defaults, "budget_limit", 1000);
    block_over_budget = query_reward_package_int(source_config, defaults, "block_over_budget", 0);
    block_disallowed = query_reward_package_int(source_config, defaults, "block_disallowed_fields", 0);
    allow_attachments = query_reward_package_int(source_config, defaults, "allow_attachments", 1);
    allow_title = query_reward_package_int(source_config, defaults, "allow_title", 1);

    score = query_reward_package_budget_score(rewards);
    breakdown = ([]);
    foreach (field, amount in weights) {
        if (stringp(field) && intp(rewards[field]) && rewards[field] > 0)
            breakdown[field] = rewards[field] * amount;
    }
    attachment_count = query_reward_package_attachment_count(rewards);
    if (attachment_count > 0)
        breakdown["attachments"] = attachment_count * query_mapping_int(extra_scores, "attachment", 200);
    if (stringp(rewards["title"]) && rewards["title"] != "")
        breakdown["title"] = query_mapping_int(extra_scores, "title", 1000);

    violations = ({ });
    if (!source_known && !query_mapping_int(defaults, "allow_unknown_source", 1)) {
        violations += ({ ([ "type": "unknown_source", "source": reward_source ]) });
        constraint_violations += 1;
    }
    foreach (field, value in rewards) {
        if (!stringp(field) || field == "item_count")
            continue;
        if (intp(value) && value > 0
            && !reward_package_allows_numeric_field(field, allowed_fields)) {
            violations += ({ ([ "type": "disallowed_numeric_field", "field": field ]) });
            constraint_violations += 1;
        }
    }
    if (!allow_attachments && attachment_count > 0) {
        violations += ({ ([ "type": "attachments_not_allowed", "count": attachment_count ]) });
        constraint_violations += 1;
    }
    if (!allow_title && stringp(rewards["title"]) && rewards["title"] != "") {
        violations += ({ ([ "type": "title_not_allowed" ]) });
        constraint_violations += 1;
    }
    if (limit > 0 && score > limit)
        violations += ({ ([ "type": "budget_exceeded", "score": score, "limit": limit ]) });

    block_apply = (limit > 0 && score > limit && block_over_budget)
        || (constraint_violations > 0 && block_disallowed);
    return ([
        "ok": 1,
        "valid": sizeof(violations) == 0,
        "source": reward_source,
        "source_configured": source_known,
        "score": score,
        "limit": limit,
        "over_budget": score > limit ? score - limit : 0,
        "budget_unit": defaults["budget_unit"] || "reward_score",
        "allowed_numeric_fields": allowed_fields,
        "allow_attachments": allow_attachments ? 1 : 0,
        "allow_title": allow_title ? 1 : 0,
        "breakdown": breakdown,
        "violations": violations,
        "block_apply": block_apply ? 1 : 0,
    ]);
}
mapping query_reward_diminishing_source_config(string reward_source)
{
    mapping framework;
    mapping source_config;
    mapping sources;
    string prefix;
    int colon;

    if (!stringp(reward_source) || reward_source == "")
        reward_source = "reward_apply";

    framework = query_reward_diminishing_framework();
    sources = mapp(framework["sources"]) ? framework["sources"] : ([]);
    source_config = sources[reward_source];
    if (mapp(source_config))
        return copy(source_config);

    colon = strsrch(reward_source, ":");
    if (colon > 0) {
        prefix = reward_source[0..colon - 1];
        source_config = sources[prefix];
    }
    return mapp(source_config) ? copy(source_config) : ([]);
}
private string query_reward_diminishing_source(mapping options)
{
    string source;

    source = mapp(options) ? options["source"] : "";
    return stringp(source) && source != "" ? source : "reward_apply";
}
private string query_reward_diminishing_target_key(mapping rewards, mapping options)
{
    string target_key;

    target_key = mapp(options) ? options["target_key"] : "";
    if (!stringp(target_key) || target_key == "")
        target_key = mapp(rewards) ? rewards["target_key"] : "";
    return stringp(target_key) && target_key != "" ? target_key : "*";
}
private int query_reward_diminishing_int(mapping source_config, mapping defaults,
    string key, int default_value)
{
    if (mapp(source_config) && intp(source_config[key]))
        return source_config[key];
    if (mapp(defaults) && intp(defaults[key]))
        return defaults[key];
    return default_value;
}
private mixed *query_reward_diminishing_tracked_fields(mapping source_config, mapping defaults)
{
    mixed *fields;

    fields = mapp(source_config) ? source_config["tracked_numeric_fields"] : 0;
    if (!arrayp(fields) || sizeof(fields) == 0)
        fields = mapp(defaults) ? defaults["tracked_numeric_fields"] : 0;
    return arrayp(fields) && sizeof(fields) > 0
        ? fields
        : ({ "gold", "potential", "exp", "sect_contribution" });
}
private int query_tracked_reward_amount(mapping rewards, mixed *fields)
{
    mixed raw_field;
    string field;
    int amount;
    int total;

    if (!mapp(rewards) || !arrayp(fields))
        return 0;

    foreach (raw_field in fields) {
        if (!stringp(raw_field) || raw_field == "")
            continue;
        field = raw_field;
        amount = intp(rewards[field]) ? rewards[field] : 0;
        if (amount > 0)
            total += amount;
    }
    return total;
}
private mapping query_reward_diminishing_store(object player)
{
    mapping store;
    mapping legacy_store;

    if (!objectp(player))
        return ([]);
    store = player->query("reward/diminishing");
    if (mapp(store))
        return restore_variable(save_variable(store));
    /* 只读兼容旧会话态；新事件一律写入持久 owner。 */
    legacy_store = player->query_temp("numeric/reward_diminishing");
    return mapp(legacy_store)
        ? restore_variable(save_variable(legacy_store)) : ([]);
}
private string query_reward_diminishing_state_key(string reward_source, string target_key)
{
    if (!stringp(reward_source) || reward_source == "")
        reward_source = "reward_apply";
    if (!stringp(target_key) || target_key == "")
        target_key = "*";
    return reward_source + "|" + target_key;
}
mapping query_reward_diminishing_state(object player, string reward_source, string target_key)
{
    mapping state;
    mapping store;

    if (!objectp(player))
        return ([]);

    store = query_reward_diminishing_store(player);
    state = store[query_reward_diminishing_state_key(reward_source, target_key)];
    return mapp(state) ? copy(state) : ([]);
}
private mapping scale_reward_diminishing_rewards(mapping rewards, mixed *fields, int factor_bp)
{
    mapping result;
    mixed raw_field;
    string field;
    int amount;

    if (!mapp(rewards))
        return ([]);

    if (factor_bp < 0)
        factor_bp = 0;
    if (factor_bp > BP_BASE)
        factor_bp = BP_BASE;

    result = copy(rewards);
    foreach (raw_field in fields || ({ })) {
        if (!stringp(raw_field) || raw_field == "")
            continue;
        field = raw_field;
        amount = intp(rewards[field]) ? rewards[field] : 0;
        if (amount > 0)
            result[field] = amount * factor_bp / BP_BASE;
    }
    return result;
}
mapping query_reward_diminishing_result(object player, mapping rewards, mapping options)
{
    mapping defaults;
    mapping event;
    mapping framework;
    mapping source_config;
    mapping state;
    mixed *fields;
    string reward_source;
    string target_key;
    int elapsed;
    int enabled;
    int factor_bp;
    int last_at;
    int min_factor;
    int now;
    int repeat_count;
    int repeat_decay;
    int tracked_amount;
    int day_key;
    int state_day_key;

    if (!objectp(player) || !mapp(rewards))
        return ([ "ok": 0, "error": "invalid_arguments" ]);

    framework = query_reward_diminishing_framework();
    defaults = mapp(framework["defaults"]) ? framework["defaults"] : ([]);
    reward_source = query_reward_diminishing_source(options);
    target_key = query_reward_diminishing_target_key(rewards, options);
    source_config = query_reward_diminishing_source_config(reward_source);
    fields = query_reward_diminishing_tracked_fields(source_config, defaults);
    tracked_amount = query_tracked_reward_amount(rewards, fields);
    enabled = query_reward_diminishing_int(source_config, defaults, "enabled", 0);

    /* 离线结算收益是固定收益，不进入主动玩法的每日递减池。调用方用
     * offline=1 标记；兼容旧 source 名称也 fail-open 到固定收益。 */
    if (!enabled || tracked_amount <= 0
        || (mapp(options) && options["offline"])
        || strsrch(reward_source, "idle") == 0
        || strsrch(reward_source, "offline") == 0) {
        return ([
            "ok": 1,
            "enabled": enabled ? 1 : 0,
            "tracked": tracked_amount > 0 ? 1 : 0,
            "source": reward_source,
            "target_key": target_key,
            "factor_bp": BP_BASE,
            "repeat_count": 0,
            "elapsed_seconds": 0,
            "rewards": copy(rewards),
            "event": ([ "enabled": 0, "tracked": 0,
                "offline_exempt": (mapp(options) && options["offline"])
                    || strsrch(reward_source, "idle") == 0
                    || strsrch(reward_source, "offline") == 0 ? 1 : 0 ]),
        ]);
    }

    now = time();
    day_key = TIME_D->query_day();
    state = query_reward_diminishing_state(player, reward_source, target_key);
    last_at = intp(state["last_at"]) ? state["last_at"] : 0;
    state_day_key = intp(state["day_key"]) ? state["day_key"] : -1;
    elapsed = last_at > 0 ? now - last_at : 0;
    if (elapsed < 0)
        elapsed = 0;

    /* S20/L-165：按自然日固定计数，同一来源下再按 target_key 分开；跨日
     * 自动从第 1 次重新计算，不再以短时间窗口决定是否递减。 */
    repeat_count = state_day_key == day_key
        ? (state["count"] || 0) + 1 : 1;
    if (repeat_count <= 0)
        repeat_count = 1;

    repeat_decay = query_reward_diminishing_int(source_config, defaults, "repeat_decay_bp", 1000);
    min_factor = query_reward_diminishing_int(source_config, defaults, "min_factor_bp", 2000);
    if (min_factor < 0)
        min_factor = 0;
    if (min_factor > BP_BASE)
        min_factor = BP_BASE;

    factor_bp = BP_BASE - (repeat_count - 1) * repeat_decay;
    if (factor_bp < min_factor)
        factor_bp = min_factor;
    if (factor_bp > BP_BASE)
        factor_bp = BP_BASE;

    event = ([
        "enabled": 1,
        "tracked": 1,
        "source": reward_source,
        "target_key": target_key,
        "now": now,
        "count": repeat_count,
        "factor_bp": factor_bp,
        "elapsed_seconds": elapsed,
        "day_key": day_key,
    ]);

    return ([
        "ok": 1,
        "enabled": 1,
        "tracked": 1,
        "source": reward_source,
        "target_key": target_key,
        "factor_bp": factor_bp,
        "repeat_count": repeat_count,
        "elapsed_seconds": elapsed,
        "rewards": scale_reward_diminishing_rewards(rewards, fields, factor_bp),
        "event": event,
    ]);
}
int record_reward_diminishing_event(object player, mapping event)
{
    mapping store;
    mapping response;
    mapping change;
    string state_key;

    if (!objectp(player) || !mapp(event) || !event["enabled"] || !event["tracked"])
        return 1;

    store = query_reward_diminishing_store(player);
    state_key = query_reward_diminishing_state_key(event["source"], event["target_key"]);
    store[state_key] = ([
        "last_at": event["now"] || time(),
        "count": event["count"] || 1,
        "factor_bp": event["factor_bp"] || BP_BASE,
        "elapsed_seconds": event["elapsed_seconds"] || 0,
        "day_key": event["day_key"] || TIME_D->query_day(),
    ]);
    change = ([ "reward/diminishing": store ]);
    response = PLAYER_STATE_D->commit_state_fields(player, change,
        PLAYER_STATE_D->build_state_idempotency_key(player,
            "reward/diminishing_event", change));
    return mapp(response) && response["ok"];
}
int clear_reward_diminishing_state(object player, string reward_source, string target_key)
{
    mapping store;
    mapping response;
    mapping change;
    string state_key;

    if (!objectp(player))
        return 0;

    store = query_reward_diminishing_store(player);
    state_key = query_reward_diminishing_state_key(reward_source, target_key);
    if (mapp(store))
        map_delete(store, state_key);
    change = ([ "reward/diminishing": store ]);
    response = PLAYER_STATE_D->commit_state_fields(player, change,
        PLAYER_STATE_D->build_state_idempotency_key(player,
            "reward/diminishing_clear", change));
    return mapp(response) && response["ok"];
}
