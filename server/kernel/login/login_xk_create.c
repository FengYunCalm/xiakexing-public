/**
 * @file    login_xk_create.c
 * @brief   登录 XK 建角辅助子服务
 * @author  XiaKeXing Development Team
 * @date    2026-03-30
 * @version 1.0.0
 */

#pragma strict_types

#include <system/globals.h>
#include <system/account.h>

#define LOGIN_AUTH      "/kernel/login/login_auth"
#define LOGIN_SESSION   "/kernel/login/login_session"
#define PLAYER_D        "/daemons/services/player/player_d"

private void clear_xk_create_pending_state(object login_ob)
{
    if (!objectp(login_ob))
        return;

    login_ob->set_pending_username("");
    login_ob->set_pending_password("");
    login_ob->set_pending_name("");
    login_ob->set_pending_gender("");
    login_ob->set_reg_stage(0);
}

string handle_xk_create_json(object login_ob, string json_str)
{
    string account_name;
    string char_name;
    string temp_password;
    string gender;
    string normalized_gender;
    int is_existing_account;
    mapping payload;
    mixed err;

    err = catch(payload = json_decode(json_str));
    if (err || !mapp(payload)) {
        clear_xk_create_pending_state(login_ob);
        login_ob->emit_login_feedback(login_ob->query_login_player(), "角色创建请求格式错误", "error");
        return "__protocol__";
    }

    account_name = payload["account"];
    char_name = payload["name"];
    temp_password = payload["password"];
    gender = payload["gender"];

    if (!account_name || account_name == "") {
        clear_xk_create_pending_state(login_ob);
        login_ob->emit_login_feedback(login_ob->query_login_player(), "账号名称不能为空", "error");
        return "__protocol__";
    }

    if (!call_other(LOGIN_AUTH, "valid_username", account_name)) {
        clear_xk_create_pending_state(login_ob);
        login_ob->emit_login_feedback(login_ob->query_login_player(), "账号格式不正确", "error");
        return "__protocol__";
    }

    // 建角/验证密码都会触发 bcrypt 计算：安全预算必须先于任何密码验证消耗，
    // 否则攻击者可绕过限流反复验密码（原顺序在下面 existing 分支之后才检查）。
    if (!login_ob->validate_login_security(account_name)) {
        clear_xk_create_pending_state(login_ob);
        return "__protocol__";
    }

    is_existing_account = call_other(PLAYER_D, "account_exists", account_name);
    if (is_existing_account) {
        if (!call_other(PLAYER_D, "validate_password", account_name, temp_password)) {
            clear_xk_create_pending_state(login_ob);
            login_ob->emit_login_feedback(login_ob->query_login_player(), "账号或密码错误", "error");
            return "__protocol__";
        }

        if (call_other(PLAYER_D, "account_has_character", account_name)) {
            clear_xk_create_pending_state(login_ob);
            login_ob->emit_login_feedback(login_ob->query_login_player(), "该账号已创建角色", "error");
            return "__protocol__";
        }
    }

    if (!is_existing_account && !CONFIG_D->query_registration_enabled()) {
        clear_xk_create_pending_state(login_ob);
        login_ob->emit_login_feedback(login_ob->query_login_player(), "当前暂未开放新账号注册", "error");
        return "__protocol__";
    }

    if (!stringp(temp_password) || temp_password == "") {
        clear_xk_create_pending_state(login_ob);
        login_ob->emit_login_feedback(login_ob->query_login_player(), "密码不能为空", "error");
        return "__protocol__";
    }

    if (!is_existing_account && !call_other(PLAYER_D, "validate_password_format", temp_password)) {
        clear_xk_create_pending_state(login_ob);
        login_ob->emit_login_feedback(login_ob->query_login_player(),
            sprintf("密码长度必须为 %d-%d 字节（一个汉字约占 3 字节）", CONFIG_D->query_password_min_len(), ACCOUNT_MAX_PASSWORD_LENGTH), "error");
        return "__protocol__";
    }

    if (char_name && char_name != "" && !call_other(LOGIN_AUTH, "valid_chinese_name", char_name)) {
        clear_xk_create_pending_state(login_ob);
        login_ob->emit_login_feedback(login_ob->query_login_player(), "角色名格式不正确", "error");
        return "__protocol__";
    }

    normalized_gender = login_ob->parse_gender_input(gender || "");
    if (stringp(gender) && gender != "" && !stringp(normalized_gender)) {
        clear_xk_create_pending_state(login_ob);
        login_ob->emit_login_feedback(login_ob->query_login_player(), "性别格式不正确", "error");
        return "__protocol__";
    }
    if (!stringp(normalized_gender) || normalized_gender == "")
        normalized_gender = "男性";

    err = catch {
        clear_xk_create_pending_state(login_ob);
        login_ob->set_pending_username(account_name);
        login_ob->set_pending_password(temp_password);
        login_ob->set_pending_name(char_name || "");
        login_ob->set_pending_gender(normalized_gender);

        if (login_ob->finalize_character_creation_for_account(is_existing_account)) {
            object char_ob;

            char_ob = login_ob->query_player_ob();
            if (!objectp(char_ob) || !login_ob->finish_login_transition(char_ob, "角色创建成功！", 1))
            login_ob->emit_login_feedback(login_ob->query_login_player(), "角色已创建，但连接切换失败", "error");
        } else {
            clear_xk_create_pending_state(login_ob);
        login_ob->emit_login_feedback(login_ob->query_login_player(), "角色创建失败", "error");
        }
    };

    if (err) {
        clear_xk_create_pending_state(login_ob);
        login_ob->emit_login_feedback(login_ob->query_login_player(), "创建过程出错: " + err, "error");
    }

    return "__protocol__";
}
