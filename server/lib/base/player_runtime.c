/**
 * @file    player_runtime.c
 * @brief   玩家运行时子模块



 *
 * @par Description:
 * 基础对象层子模块，供上层对象组合能力。
 */

#pragma strict_types

#include <system/services.h>

#undef is_player

mixed query(string path);

private nosave object _player_service;

private object resolve_player_service()
{
    object player_service;

    if (objectp(_player_service))
        return _player_service;

    player_service = find_object(PLAYER_D);
    if (!objectp(player_service))
        player_service = load_object(PLAYER_D);

    if (objectp(player_service))
        _player_service = player_service;

    return player_service;
}

int is_player()
{
    return 1;
}

int userp()
{
    return 1;
}

int is_wizard()
{
    object player_service;
    string my_id;

    my_id = get_object_id(this_object());
    if (!stringp(my_id) || my_id == "")
        return 0;

    player_service = resolve_player_service();

    if (objectp(player_service) && function_exists("is_admin_account", player_service))
        return player_service->is_admin_account(my_id);

    return 0;
}
