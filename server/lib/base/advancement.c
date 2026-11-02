/**
 * @file    advancement.c
 * @brief   成长子模块



 *
 * @par Description:
 * 基础对象层子模块，供上层对象组合能力。
 */

#pragma strict_types

#include <system/services.h>

mixed query(string path);
int set(string path, mixed value);

void set_skill_cap_rank(int rank)
{
    int required_exp;

    if (rank < 1) rank = 1;
    required_exp = NUMERIC_RULE_D->query_required_exp_for_progression_rank(rank);
    if ((query("combat_exp") || 0) < required_exp)
        set("combat_exp", required_exp);
    set("progression_rank", rank);
    if (function_exists("invalidate_xk_vital_maxima_projection", this_object()))
        call_other(this_object(), "invalidate_xk_vital_maxima_projection", 1);
    if (function_exists("invalidate_xk_score_projection", this_object()))
        call_other(this_object(), "invalidate_xk_score_projection");
}

int query_skill_cap_rank()
{
    return NUMERIC_RULE_D->query_player_progression_rank(this_object());
}

void add_skill_cap_rank(int n)
{
    set_skill_cap_rank(query_skill_cap_rank() + n);
}

void set_exp(int exp)
{
    if (exp < 0) exp = 0;
    set("combat_exp", exp);
    if (function_exists("invalidate_xk_vital_maxima_projection", this_object()))
        call_other(this_object(), "invalidate_xk_vital_maxima_projection", 1);
    if (function_exists("invalidate_xk_score_projection", this_object()))
        call_other(this_object(), "invalidate_xk_score_projection");
}

int query_exp()
{
    return query("combat_exp") || 0;
}

void add_exp(int amount)
{
    set_exp(query_exp() + amount);
}
