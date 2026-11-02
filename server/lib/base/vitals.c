/**
 * @file    vitals.c
 * @brief   生命值子模块



 *
 * @par Description:
 * 基础对象层子模块，供上层对象组合能力。
 */

#pragma strict_types

#include <system/services.h>

mixed query(string path);
int set(string path, mixed value);
int delete(string path);
mixed query_temp(string path);
int set_temp(string path, mixed value);
string name();
void die();
int query_max_hp();
int query_max_mp();
int query_max_ep();
void heal_mp(int amount);
void heal_ep(int amount);
mapping query_vital_maxima_snapshot();
void cache_vital_maxima_snapshot(mapping maxima);

private string query_vital_maxima_cache_token()
{
    int projection_generation;

    projection_generation = function_exists(
        "query_xk_vital_maxima_generation", this_object())
            ? this_object()->query_xk_vital_maxima_generation() : 0;
    return sprintf("%d|%d|%d|%d|%d|%d",
        NUMERIC_RULE_D->query_numeric_config_generation(),
        SKILL_D->query_skill_config_generation(),
        projection_generation,
        query("combat/max_hp") || 100,
        query("combat/max_mp") || 100,
        query("combat/max_ep") || 100);
}

private int normalize_numeric_vital_max(mapping maxima, string resource,
    string stored_path, int default_value)
{
    mixed raw_value;
    int max_value;

    raw_value = mapp(maxima) ? maxima[resource] : 0;
    if ((!intp(raw_value) || raw_value <= 0) && mapp(maxima))
        raw_value = maxima["max_" + resource];
    max_value = intp(raw_value) ? raw_value : 0;
    if (max_value <= 0)
        max_value = query(stored_path) || default_value;
    return max_value > 0 ? max_value : default_value;
}

private void cache_numeric_vital_maxima(mapping maxima, int now)
{
    set_temp("vitals/max_cache/hp", ([
        "at": now,
        "value": normalize_numeric_vital_max(maxima, "hp", "combat/max_hp", 100),
    ]));
    set_temp("vitals/max_cache/mp", ([
        "at": now,
        "value": normalize_numeric_vital_max(maxima, "mp", "combat/max_mp", 100),
    ]));
    set_temp("vitals/max_cache/ep", ([
        "at": now,
        "value": normalize_numeric_vital_max(maxima, "ep", "combat/max_ep", 100),
    ]));
}

/* Publish all three derived maxima at the same semantic instant. Status
 * emission already paid for the derived-attribute calculation; retaining the
 * snapshot lets the immediately queued score refresh consume those facts
 * without re-running the rule graph. */
void cache_vital_maxima_snapshot(mapping maxima)
{
    int now;
    mapping values;

    if (!mapp(maxima))
        return;
    now = time();
    cache_numeric_vital_maxima(maxima, now);
    values = ([
        "hp": normalize_numeric_vital_max(maxima, "hp", "combat/max_hp", 100),
        "mp": normalize_numeric_vital_max(maxima, "mp", "combat/max_mp", 100),
        "ep": normalize_numeric_vital_max(maxima, "ep", "combat/max_ep", 100),
    ]);
    set_temp("vitals/max_cache/snapshot", ([
        "at": now,
        "input_token": query_vital_maxima_cache_token(),
        "values": values,
    ]));
}

mapping query_vital_maxima_snapshot()
{
    mapping cache;
    mapping maxima;
    string input_token;

    input_token = query_vital_maxima_cache_token();
    cache = query_temp("vitals/max_cache/snapshot");
    if (mapp(cache) && mapp(cache["values"])
        && cache["input_token"] == input_token)
        return copy(cache["values"]);

    maxima = NUMERIC_RULE_D->query_player_max_resources(this_object());
    cache_vital_maxima_snapshot(maxima);
    cache = query_temp("vitals/max_cache/snapshot");
    return mapp(cache) && mapp(cache["values"])
        ? copy(cache["values"])
        : ([
            "hp": normalize_numeric_vital_max(maxima, "hp", "combat/max_hp", 100),
            "mp": normalize_numeric_vital_max(maxima, "mp", "combat/max_mp", 100),
            "ep": normalize_numeric_vital_max(maxima, "ep", "combat/max_ep", 100),
        ]);
}

private int query_numeric_vital_max(string resource, string stored_path, int default_value)
{
    mapping maxima;

    maxima = query_vital_maxima_snapshot();
    return normalize_numeric_vital_max(maxima, resource, stored_path, default_value);
}

/* 返回 1=写入成功；0=状态 owner 拒绝/提交失败。
 * 写入失败时不再刷新投影、也不触发 die()：否则会在真实余额没变的情况下
 * 广播“已恢复/已死亡”的假象（L-24）。 */
int set_hp(int hp)
{
    int max_hp = query_max_hp();

    if (hp < 0) hp = 0;
    if (hp > max_hp) hp = max_hp;

    if (!set("combat/hp", hp))
        return 0;
    if (function_exists("schedule_owner_hp_read_model_refresh", this_object()))
        call_other(this_object(), "schedule_owner_hp_read_model_refresh");
    if (function_exists("invalidate_xk_score_projection", this_object()))
        call_other(this_object(), "invalidate_xk_score_projection");

    if (hp == 0 && living(this_object())) {
        die();
    }

    return 1;
}

int query_hp()
{
    return query("combat/hp") || 0;
}

int query_max_hp()
{
    return query_numeric_vital_max("hp", "combat/max_hp", 100);
}

int heal_hp(int amount)
{
    return set_hp(query_hp() + amount);
}

/* 返回 1=恢复已提交；0=类型不支持或状态写入失败（调用方不得据此发成功回执）。 */
int receive_healing(string type, int amount)
{
    if (type == "hp" || type == "qi")
        return heal_hp(amount);

    if (type == "mp" || type == "neili")
        return heal_mp(amount);

    if (type == "ep" || type == "jingli")
        return heal_ep(amount);

    return 0;
}

int set_mp(int mp)
{
    int max_mp = query_max_mp();

    if (mp < 0) mp = 0;
    if (mp > max_mp) mp = max_mp;

    if (!set("combat/mp", mp))
        return 0;
    if (function_exists("schedule_owner_hp_read_model_refresh", this_object()))
        call_other(this_object(), "schedule_owner_hp_read_model_refresh");
    if (function_exists("invalidate_xk_score_projection", this_object()))
        call_other(this_object(), "invalidate_xk_score_projection");

    return 1;
}

int set_ep(int ep)
{
    int max_ep = query_max_ep();

    if (ep < 0) ep = 0;
    if (ep > max_ep) ep = max_ep;

    if (!set("combat/ep", ep))
        return 0;
    if (function_exists("invalidate_xk_score_projection", this_object()))
        call_other(this_object(), "invalidate_xk_score_projection");

    return 1;
}

int query_ep()
{
    return query("combat/ep") || 0;
}

int query_max_ep()
{
    return query_numeric_vital_max("ep", "combat/max_ep", 100);
}

int heal_ep(int amount)
{
    return set_ep(query_ep() + amount);
}

int consume_ep(int amount)
{
    if (query_ep() < amount) return 0;
    return set_ep(query_ep() - amount);
}

int query_mp()
{
    return query("combat/mp") || 0;
}

int query_max_mp()
{
    return query_numeric_vital_max("mp", "combat/max_mp", 100);
}

int heal_mp(int amount)
{
    return set_mp(query_mp() + amount);
}

int consume_mp(int amount)
{
    if (query_mp() < amount) return 0;
    return set_mp(query_mp() - amount);
}
