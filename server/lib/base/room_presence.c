/**
 * @file    room_presence.c
 * @brief   房间存在感子模块



 *
 * @par Description:
 * 基础对象层子模块，供上层对象组合能力。
 * 这里保留的是 presence hook 默认壳，真正的房间规则由上层按需覆写。
 */

#pragma strict_types

int valid_enter(object ob)
{
    /* 默认放行，组合该模块的房间自行追加准入边界。 */
    return 1;
}

int valid_leave(object ob, string dir)
{
    /* 默认放行，避免基础层替业务层提前做离场裁决。 */
    return 1;
}

void enter(object ob)
{
    /* 空 hook 占位，兼容房间在对象进入后追加广播或状态同步。 */
}

void leave(object ob)
{
    /* 空 hook 占位，保持与 enter 对称，供上层按需实现副作用。 */
}
