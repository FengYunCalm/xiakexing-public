/**
 * @file    login_input.c
 * @brief   登录输入处理
 * @author  XiaKeXing Development Team
 * @date    2026-03-20
 * @version 2.1.1
 *
 * @par Description:
 * 处理用户输入和 XK 协议消息，从 `login.c` 拆分出来。
 */

#pragma strict_types

#include <system/globals.h>
#include <system/account.h>
#include <system/daemon.h>
#include <system/errors.h>
#include <system/services.h>
#include <system/xk_protocol.h>
#include <system/ansi.h>
#include <framework/message_facade.h>

#define LOGIN_AUTH      "/kernel/login/login_auth"
#define LOGIN_SESSION   "/kernel/login/login_session"
#define LOGIN_INPUT_SUPPORT "/kernel/login/login_input_support"
#define LOGIN_INPUT_INGRESS "/kernel/login/login_input_ingress"
#define LOGIN_INPUT_ENDPOINT "/kernel/login/login_input_endpoint"
#define LOGIN_INPUT_PROTOCOL_ENTRY "/kernel/login/login_input_protocol_entry"
#define LOGIN_INPUT_SECURITY "/kernel/login/login_input_security"
#define LOGIN_ERROR_SNAPSHOT "/kernel/login/login_error_snapshot"
#define LOGIN_FINALIZE "/kernel/login/login_finalize"
#define LOGIN_SECURITY_FLOW "/kernel/login/login_security_flow"
#define LOGIN_CONTEXT "/kernel/login/login_context"
#define LOGIN_PLAYER_DISPATCH "/kernel/login/login_player_dispatch"
#define LOGIN_TEXT_ROUTER "/kernel/login/login_text_router"
#define LOGIN_XK_CREATE "/kernel/login/login_xk_create"
#define LOGIN_TEXT_AUTH "/kernel/login/login_text_auth"
#define LOGIN_TEXT_REGISTRATION "/kernel/login/login_text_registration"
#define LOGIN_TRANSITION "/kernel/login/login_transition"
#define BANK_D          "/daemons/services/economy/bank_d"

#define MAX_INPUT_LENGTH    256

/* 来自 login_state.c */
void init_login_state();
void reset_registration_state();
int query_reg_stage();
void set_reg_stage(int stage);
int query_is_logged_in();
void set_is_logged_in(int status);
int query_is_existing_account();
void set_is_existing_account(int flag);
string query_pending_username();
void set_pending_username(string name);
object query_player_ob();
void set_player_ob(object ob);
object query_login_player();
void set_login_player(object ob);
string parse_gender_input(string input);
void handle_registration_cancel();
int process_character_name_input(string trimmed_input);
int process_character_gender_input(string trimmed_input);
int finalize_character_creation();
int finalize_character_creation_for_account(int is_existing_account);

/* 来自 login.c */
void delayed_send_logn();
void logon();
int start_character_creation(string username, string password, string mode);
void send_room_after_exec(object player, object room);
int handoff_login_connection(object player, object login_ob);
string handle_xk_protocol(string str, object login_ob);

private mapping write_runtime_error_snapshot(mixed err)
{
    return call_other(LOGIN_ERROR_SNAPSHOT, "write_runtime_error_snapshot", err);
}

string trim_input(string input)
{
    return call_other(LOGIN_INPUT_SUPPORT, "trim_input", input);
}

int is_cancel_command(string input)
{
    return call_other(LOGIN_INPUT_SUPPORT, "is_cancel_command", input);
}

private object current_login_recipient()
{
    return call_other(LOGIN_CONTEXT, "current_login_recipient", this_object());
}

private string query_login_ip()
{
    return call_other(LOGIN_CONTEXT, "query_login_ip", this_object());
}

void emit_login_feedback(object target, string message, string level)
{
    call_other(LOGIN_CONTEXT, "emit_login_feedback", this_object(), target, message, level);
}

varargs int validate_login_security(string account)
{
    return call_other(LOGIN_SECURITY_FLOW, "validate_login_security", this_object(), account);
}

private void reset_login_security_window()
{
    call_other(LOGIN_SECURITY_FLOW, "reset_login_security_window", this_object());
}

int complete_login_handoff(object player, int destruct_delay)
{
    if (!objectp(player))
        return 0;

    if (!call_other(LOGIN_TRANSITION, "complete_login_handoff", this_object(), player))
        return 0;

    call_out("auto_look", 1, file_name(player));
    call_out("delayed_destruct", destruct_delay);

    return 1;
}

int finish_login_transition(object player, string success_message, int destruct_delay)
{
    return call_other(LOGIN_FINALIZE, "finish_login_transition",
        this_object(), player, success_message, destruct_delay);
}

string handle_xk_create_json(string json_str)
{
    return call_other(LOGIN_INPUT_PROTOCOL_ENTRY, "handle_xk_create_json", this_object(), json_str);
}

/**
 * @brief 验证并解析账号密码
 * @param input 输入字符串
 * @return mapping 包含 username, password, valid
 */
private mapping parse_credentials(string input)
{
    return call_other(LOGIN_TEXT_AUTH, "parse_credentials", input);
}

/**
 * @brief 验证用户名格式
 * @param username 用户名
 * @return int 1=有效, 0=无效
 */
private int validate_username_format(string username)
{
    return call_other(LOGIN_TEXT_AUTH, "validate_username_format", username);
}

/**
 * @brief 处理用户登录认证
 * @param username 用户名
 * @param password 密码
 * @return int 1=成功, 0=失败
 */
private int handle_user_authentication(string username, string password)
{
    return call_other(LOGIN_TEXT_AUTH, "handle_user_authentication", this_object(), username, password);
}

private string dispatch_player_command(string input) {
    return call_other(LOGIN_PLAYER_DISPATCH, "dispatch_player_command", this_object(), input);
}

/**
 * @brief 处理标准登录输入
 * @param str 输入字符串
 * @return 处理结果字符串
 */
string process_standard_login(string str)
{
    return call_other(LOGIN_INPUT_PROTOCOL_ENTRY, "process_standard_login", this_object(), str);
}

/**
 * @brief 处理主输入
 * @param str 输入字符串
 * @return 处理结果
 */
string process_input(string str)
{
    string trimmed_input;
    mixed err;
    string result;

    // 检查输入长度
    if (strlen(str) > MAX_INPUT_LENGTH) {
        emit_login_feedback(current_login_recipient(),
            "输入过长，请重新输入。", "error");
        return "";
    }

    trimmed_input = trim_input(str);

    if (trimmed_input == "" && query_reg_stage() == 0) {
        return "";
    }

    /*
     * 输入分流优先级：
     * 1. 注册阶段取消命令
     * 2. 已有 player 对象时，把命令交给 player 侧
     * 3. XK 协议消息
     * 4. 旧式文本登录/注册输入
     *
     * 这样做的目的是让“协议登录”和“传统文本登录”共存，
     * 同时保证登录态向游戏态切换期间不会吞命令。
     */
    err = catch {
        result = call_other(LOGIN_INPUT_INGRESS, "process_login_ingress",
            this_object(), str, trimmed_input);
    };

    if (err) {
        call_other(LOGIN_ERROR_SNAPSHOT, "finalize_runtime_error_snapshot", this_object(), err);
        return "";
    }

    return result;
}

/**
 * @brief 接收消息
 * @param msgclass 消息类别
 * @param msg 消息内容
 */
void receive_message(string msgclass, string msg)
{
    call_other(LOGIN_INPUT_ENDPOINT, "receive_message", this_object(), msgclass, msg);
}
