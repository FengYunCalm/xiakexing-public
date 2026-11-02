/**
 * @file    login_text_registration.c
 * @brief   登录文本注册辅助子服务
 * @author  XiaKeXing Development Team
 * @date    2026-03-30
 * @version 1.0.0
 */

#pragma strict_types

void handle_name_stage(object login_ob, string input)
{
    call_other(login_ob, "process_character_name_input", input);
}

void handle_gender_stage(object login_ob, string input)
{
    object player;

    if (!call_other(login_ob, "process_character_gender_input", input))
        return;

    if (!call_other(login_ob, "finalize_character_creation"))
        return;

    player = call_other(login_ob, "query_player_ob");
    if (objectp(player) && !call_other(login_ob, "finish_login_transition", player, "角色创建成功！", 2))
        call_other(login_ob, "emit_login_feedback", login_ob, "进入游戏连接切换失败", "error");
}

void handle_cancel_input(object login_ob, int stage)
{
    if (stage != 0)
        call_other(login_ob, "handle_registration_cancel");
}
