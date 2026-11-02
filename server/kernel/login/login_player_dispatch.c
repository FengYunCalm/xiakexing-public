/**
 * @file    login_player_dispatch.c
 * @brief   登录后短窗口命令转发辅助子服务
 * @author  XiaKeXing Development Team
 * @date    2026-03-30
 * @version 1.0.0
 */

#pragma strict_types

string dispatch_player_command(object login_ob, string input)
{
    object player;
    string expanded_input;
    int command_handled;

    player = call_other(login_ob, "query_player_ob");
    if (!objectp(player) || !stringp(input) || input == "")
        return "";

    expanded_input = player->process_input(input);
    if (!stringp(expanded_input) || expanded_input == "")
        return "";

    command_handled = player->command_hook(expanded_input);
    if (command_handled)
        return "";

    call_other(login_ob, "emit_login_feedback", player, "What?", "info");
    return "";
}
