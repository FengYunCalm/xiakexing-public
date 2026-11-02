/**
 * @file    login_finalize.c
 * @brief   登录完成收尾辅助子服务
 * @author  XiaKeXing Development Team
 * @date    2026-03-30
 * @version 1.0.0
 */

#pragma strict_types

#define LOGIN_SECURITY_FLOW "/kernel/login/login_security_flow"
#define LOGIN_SESSION "/kernel/login/login_session"

int finish_login_transition(object login_ob, object player, string success_message, int destruct_delay)
{
    mapping started_at;
    int result;

    started_at = call_other(LOGIN_SESSION, "begin_loadtest_login_phase_timing");
    if (!objectp(login_ob) || !objectp(player))
        result = 0;
    else if (!login_ob->complete_login_handoff(player, destruct_delay)) {
        if (login_ob->query_player_ob() == player)
            login_ob->set_player_ob(0);
        login_ob->set_is_logged_in(0);
        result = 0;
    } else {
        login_ob->set_player_ob(player);
        login_ob->set_is_logged_in(1);
        call_other(LOGIN_SECURITY_FLOW, "reset_login_security_window", login_ob);

        if (stringp(success_message) && success_message != "")
            login_ob->emit_login_feedback(player, success_message, "success");

        result = 1;
    }
    call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
        "handoff", started_at, result);
    return result;
}
