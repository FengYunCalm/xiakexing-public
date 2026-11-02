/**
 * @file    living_action_state.c
 * @brief   活体动作状态子模块



 *
 * @par Description:
 * 基础对象层子模块，供上层对象组合能力。
 */

#pragma strict_types

int is_busy();
int query_hp();
int query_max_hp();
int heal_hp(int amount);
int query_mp();
int query_max_mp();
int heal_mp(int amount);
int set_temp(string path, mixed value);
int delete_temp(string path);

int is_living_entity()
{
    return living(this_object());
}

int can_act()
{
    if (!is_living_entity()) return 0;
    if (is_busy()) return 0;
    return 1;
}

void continue_busy()
{
    if (!is_busy()) {
        delete_temp("action/busy");
    }
}

void heart_beat()
{
    continue_busy();
}
