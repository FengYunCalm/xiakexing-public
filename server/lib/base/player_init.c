/**
 * @file    player_init.c
 * @brief   玩家初始化子模块



 *
 * @par Description:
 * 基础对象层子模块，供上层对象组合能力。
 */

#pragma strict_types

#include <system/services.h>

string query_name();
mixed query(string path);
int set(string path, mixed value);
void set_id(mixed id);

private int resolve_legacy_vital_fallback(string legacy_path, int default_value)
{
    int value;

    value = query(legacy_path);
    if (intp(value) && value > 0)
        return value;

    return default_value;
}

private int resolve_numeric_vital_max(string resource, string legacy_path, int default_value)
{
    int numeric_value;
    int legacy_value;

    legacy_value = resolve_legacy_vital_fallback(legacy_path, default_value);
    numeric_value = NUMERIC_RULE_D->query_player_max_resource(this_object(), resource);
    if (numeric_value > legacy_value)
        return numeric_value;
    return legacy_value;
}

string init_identity()
{
    string my_id;

    my_id = get_object_id(this_object());
    if (!stringp(my_id) || my_id == "")
        my_id = query_name();

    if (!stringp(my_id) || my_id == "") {
        set("id", ({ "unknown" }));
        set_id("unknown");
        my_id = "unknown";
    }

    return my_id;
}

void init_attributes(string my_id)
{
    if (!query("name")) {
        set("name", my_id);
    }
    if (!query("gender")) {
        set("gender", "男");
    }
}

void init_combat_stats()
{
    int max_hp;
    int hp;
    int max_mp;
    int mp;
    int max_ep;
    int ep;

    if (!query("combat/max_hp")) {
        max_hp = resolve_numeric_vital_max("hp", "max_qi", 100);
        set("combat/max_hp", max_hp);
    } else {
        max_hp = query("combat/max_hp");
    }

    if (!query("combat/hp")) {
        hp = resolve_legacy_vital_fallback("qi", max_hp);
        if (hp > max_hp)
            hp = max_hp;
        set("combat/hp", hp);
    }

    if (!query("combat/max_mp")) {
        max_mp = resolve_numeric_vital_max("mp", "max_jing", 100);
        set("combat/max_mp", max_mp);
    } else {
        max_mp = query("combat/max_mp");
    }

    if (!query("combat/mp")) {
        mp = resolve_legacy_vital_fallback("jing", max_mp);
        if (mp > max_mp)
            mp = max_mp;
        set("combat/mp", mp);
    }

    if (!query("combat/max_ep")) {
        max_ep = resolve_numeric_vital_max("ep", "max_ep", 100);
        set("combat/max_ep", max_ep);
    } else {
        max_ep = query("combat/max_ep");
    }

    if (!query("combat/ep")) {
        ep = max_ep;
        set("combat/ep", ep);
    }
}

void init_skills()
{
    if (!query("skills")) {
        set("skills", ([]));
    }

    if (!query("skill_exp")) {
        set("skill_exp", ([]));
    }

    if (!query("combat_exp")) {
        set("combat_exp", 0);
    }
}
