/**
 * @file    login.c
 * @brief   登录与注册系统门面层
 * @author  XiaKeXing Development Team
 * @date    2026-03-20
 * @version 2.1.1
 *
 * @par Description:
 * 登录系统的门面层，负责协调各个子模块完成用户认证和角色管理。
 * 主文件已精简，具体实现已拆分到子模块：
 * - login_state.c : 状态管理和注册流程
 * - login_input.c : 输入处理和XK协议
 * - login_auth.c  : 认证逻辑（原有）
 * - login_session.c: 会话管理（原有）
 * - login_security.c: 安全检查（原有）
 */

#pragma strict_types

#define XK_PROTOCOL_CORE "/kernel/framework/xk_protocol_core"
#include <system/globals.h>
#include <system/daemon.h>
#include <system/services.h>
#include <system/xk_protocol.h>

#include <system/ansi.h>
#include <framework/message_facade.h>
#include <kernel/common_helpers.h>

#include "login/login.h"

private void emit_login_feedback(object target, string message, string level);
private void emit_registration_stage(object target, string stage, string message);
private void emit_login_prompt(object target);
private void send_login_capa_response(object target, mapping data);
private int bind_gateway_driver_session(object target, mixed data);
string process_input(string str);

#include "login/login_state.c"
#include "login/login_input.c"

#define LOGIN_AUTH      "/kernel/login/login_auth"
#define LOGIN_SESSION   "/kernel/login/login_session"
#define LOGIN_SECURITY  "/kernel/login/login_security"

private object resolve_login_prompt_target();

private int bind_gateway_driver_session(object target, mixed data)
{
    mapping login_route;
    mapping meta;
    mapping opened;
    string session_id;
    mixed err;
    int bound;

    if (!objectp(target) || !mapp(data))
        return 0;

    session_id = data["session_id"];
    if (!stringp(session_id) || session_id == "")
        return 0;

    meta = ([ "source": "driver_gateway" ]);
    if (stringp(data["ip"]) && data["ip"] != "")
        meta["ip"] = data["ip"];
    if (intp(data["port"]) && data["port"] > 0)
        meta["port"] = data["port"];

    opened = GATEWAY_SESSION_D->open_session(session_id, meta);
    if (!mapp(opened)) {
        log_file("system/gateway_login", sprintf(
            "[%s] gateway open failed sid=%s result=%O\n",
            ctime(time()), session_id, opened));
        return 0;
    }
    bound = GATEWAY_SESSION_D->bind_session_target(session_id, target, "login");
    if (!bound) {
        log_file("system/gateway_login", sprintf(
            "[%s] gateway target bind failed sid=%s target=%O\n",
            ctime(time()), session_id, target));
        return 0;
    }
    err = catch(login_route = OWNER_RUNTIME_D->query_service_owner_route("login", session_id));
    if (err || !mapp(login_route) || login_route["ok"] != 1) {
        log_file("system/gateway_login", sprintf(
            "[%s] gateway login route failed sid=%s error=%O route=%O\n",
            ctime(time()), session_id, err, login_route));
        return 0;
    }
    login_route["direct_cross_owner_write"] = 0;
    target->set_temp("owner/login_route", login_route);
    target->delete_temp("client/plain_text_only");
    target->set_temp("xk_protocol_version", XK_PROTOCOL_VERSION_STRING);
    return 1;
}

/**
 * @brief 对象创建时初始化
 */
void create()
{
    init_login_state();
}

mapping health_check()
{
    return ([
        "ok": function_exists("logon", this_object()) && function_exists("gateway_logon",
        this_object()),
        "service": "login",
    ]);
}

/**
 * @brief 用户登录入口
 */
void logon()
{
    object player;
    string gateway_session_id;

    gateway_session_id = GATEWAY_SESSION_D->query_object_session_id(this_object());
    log_file("system/gateway_login", sprintf("[%s] logon() on %O sid=%s\n",
        ctime(time()), this_object(), gateway_session_id || ""));

    /*
     * 登录门面在连接建立瞬间应优先把“真正持有连接的 login 对象”当作
     * 协议接收者，而不是临时依赖 `this_player()` 可能暴露出来的 driver/master
     * 过渡对象。否则延迟发送 LOGN 时就会把无效对象带进 call_out，制造日志噪音。
     */
    player = resolve_login_prompt_target();
    if (!objectp(player)) {
        player = this_object();
    }
    if (!player)
        return;

    set_login_player(player);
    set_is_logged_in(0);
    set_reg_stage(0);

    call_out("delayed_send_logn", 0);
}

void gateway_logon(mixed data)
{
    object player;
    string gateway_session_id;

    gateway_session_id = GATEWAY_SESSION_D->query_object_session_id(this_object());
    log_file("system/gateway_login", sprintf("[%s] gateway_logon() on %O sid=%s data=%O\n",
        ctime(time()), this_object(), gateway_session_id || "", data));

    player = resolve_login_prompt_target();
    if (!objectp(player))
        player = this_object();
    if (!objectp(player))
        return;

    log_file("system/gateway_login", sprintf(
        "[%s] gateway bind attempt caller=%O target=%O target_sid=%s target_interactive=%d data_sid=%s\n",
        ctime(time()), this_object(), player,
        data["session_id"] || "",
        interactive(player), data["session_id"] || ""));
    if (!bind_gateway_driver_session(player, data)) {
        log_file("system/gateway_login", sprintf(
            "[%s] gateway bind failed caller=%O target=%O data_sid=%s\n",
            ctime(time()), this_object(), player, data["session_id"] || ""));
        return;
    }
    log_file("system/gateway_login", sprintf(
        "[%s] gateway session bound owner=%O sid=%s interactive=%d login=%O\n",
        ctime(time()), player,
        data["session_id"] || "",
        interactive(player), this_object()));
    set_login_player(player);
    set_is_logged_in(0);
    set_reg_stage(0);
    emit_login_prompt(player);
}

private object resolve_login_prompt_target()
{
    object player;

    if (interactive(this_object()))
        return this_object();

    player = query_login_player();
    if (objectp(player) && interactive(player))
        return player;

    player = this_player();
    if (objectp(player) && interactive(player))
        return player;

    return 0;
}

/**
 * @brief 网络断开处理
 */
void net_dead()
{
    string gateway_session_id;

    gateway_session_id = GATEWAY_SESSION_D->query_object_session_id(this_object());
    if (stringp(gateway_session_id) && gateway_session_id != "")
        GATEWAY_SESSION_D->close_session(gateway_session_id);
    destruct(this_object());
}

/**
 * @brief 延迟发送登录提示消息
 * @param player 目标玩家对象
 */
void delayed_send_logn()
{
    object player;

    /*
     * LOGN 只应该出现在“尚未进入任何登录/注册后续状态”的初始握手阶段。
     * 新 driver 的调度时序更稳定后，原本 0 tick 的 call_out 会在注册已启动时
     * 仍然落地，导致客户端先收到 REGI 再收到多余的 LOGN prompt。
     * 这里把 LOGN 限制为纯初始提示，避免它和 REGI/登录成功后的协议流交叉。
     */
    if (query_is_logged_in() || query_reg_stage() != 0)
        return;

    player = resolve_login_prompt_target();
    if (!objectp(player))
        return;

    emit_login_prompt(player);
}

private void emit_login_prompt(object target)
{
    int last_sent_at;
    string gateway_session_id;

    if (!objectp(target))
        return;

    gateway_session_id = GATEWAY_SESSION_D->query_object_session_id(target);
    if (stringp(gateway_session_id) && gateway_session_id != "") {
        last_sent_at = target->query_temp("gateway/logn_prompt_sent_at");
        log_file("system/gateway_login", sprintf("[%s] emit_login_prompt target=%O sid=%s last=%O\n",
            ctime(time()), target, gateway_session_id, last_sent_at));
        if (intp(last_sent_at) && time() - last_sent_at < 1)
            return;
        target->set_temp("gateway/logn_prompt_sent_at", time());
    }

    call_other("/kernel/simul_efun", "xk_send_login_prompt", target);
}

private void emit_registration_stage(object target, string stage, string message)
{
    if (!objectp(target))
        target = this_object();

    call_other("/kernel/simul_efun", "xk_send_register",
        target, stage || "", message || "");
}

private void send_login_capa_response(object target, mapping data)
{
    if (!objectp(target))
        return;

    call_other(XK_PROTOCOL_CORE,
        "xk_send_capa_response", target, mapp(data) ? data : ([]));
}

/**
 * @brief 延迟销毁
 */
void delayed_destruct()
{
    destruct(this_object());
}

private int login_reject_command_envelope(mixed data)
{
    mixed nested;
    string trimmed;

    if (stringp(data)) {
        trimmed = trim(data);
        return strlen(trimmed) > 0 && trimmed[0] == '{';
    }
    if (!mapp(data))
        return 0;
    if (!undefinedp(data["schema"])
        || !undefinedp(data["command_request_id"]))
        return 1;
    nested = data["data"];
    return mapp(nested)
        && (!undefinedp(nested["schema"])
            || !undefinedp(nested["command_request_id"]));
}

string gateway_receive(mixed data)
{
    string input;
    string gateway_session_id;
    mixed nested_data;

    if (login_reject_command_envelope(data))
        return "";

    gateway_session_id = GATEWAY_SESSION_D->query_object_session_id(this_object());
    if (stringp(gateway_session_id) && gateway_session_id != "")
        GATEWAY_SESSION_D->touch_session(gateway_session_id);

    if (stringp(data))
        input = data;
    else if (mapp(data) && stringp(data["payload"]))
        input = data["payload"];
    else if (mapp(data) && stringp(data["data"]))
        input = data["data"];
    else if (mapp(data) && mapp(data["data"])) {
        nested_data = data["data"];
        if (stringp(nested_data["payload"]))
            input = nested_data["payload"];
        else if (stringp(nested_data["data"]))
            input = nested_data["data"];
        else
            return "";
    }
    else
        return "";

    return process_input(input);
}

int handoff_login_connection(object player, object login_ob)
{
    int exec_result;

    if (!objectp(player) || !objectp(login_ob)) {
        log_file("system/session", "login handoff rejected: missing object\n");
        return 0;
    }

    if (!interactive(login_ob)) {
        log_file("system/session", "login handoff rejected: login object is not interactive\n");
        return 0;
    }

    if (!call_other(LOGIN_SESSION, "can_handoff_session", player, login_ob)) {
        log_file("system/session", sprintf(
            "login handoff rejected: session check failed player=%O "
            + "login=%O player_session=%O login_session=%O\n",
            player, login_ob, player->query_temp("session_id"),
            login_ob->query_temp("session_id")));
        return 0;
    }

    exec_result = exec(player, login_ob);
    if (!exec_result) {
        log_file("system/session", sprintf(
            "login handoff rejected: exec failed player=%O login=%O\n",
            player, login_ob));
        return 0;
    }

    if (!interactive(player) || interactive(login_ob)) {
        log_file("system/session", sprintf(
            "login handoff rejected: post-exec state player_interactive=%d login_interactive=%d\n",
            interactive(player), interactive(login_ob)));
        return 0;
    }
    return 1;
}

/**
 * @brief 自动查看环境
 * @param player 玩家对象
 */
void auto_look(mixed player_ref)
{
    object env;
    object player;
    int login_time;
    int last_command_time;

    if (objectp(player_ref))
        player = player_ref;
    else if (stringp(player_ref) && player_ref != "")
        player = find_object(player_ref);

    if (!objectp(player))
        return;

    env = environment(player);
    if (!env)
        return;

    if (player->query_temp("xk_protocol_version"))
        return;

    login_time = player->query_temp("player/login_time");
    last_command_time = player->query_temp("player/last_command_time");
    if (intp(login_time) && intp(last_command_time) && last_command_time >= login_time)
        return;

    if (function_exists("command_hook", player)) {
        player->command_hook("look");
    }
}

/**
 * @brief 发送房间信息给玩家
 * @param player 玩家对象
 * @param room 房间对象
 */
void send_room_after_exec(object player, object room)
{
    if (!player || !room)
        return;

    CLIENT_SYNC_D->request_room_snapshot(player, room, 0);
}

/**
 * @brief 发送角色创建成功消息
 * @param player 玩家对象
 */
void send_char_create_success(object player)
{
    if (!player)
        return;

    call_other("/kernel/simul_efun", "xk_send_system", player, "角色创建成功！", "success");
}

/**
 * @brief 发送角色创建错误消息
 * @param error_code 错误代码
 * @param message 错误消息
 */
void send_char_create_error(string error_code, string message)
{
    object target = query_login_player() ? query_login_player() : this_object();
    call_other("/kernel/simul_efun", "xk_send_register", target, error_code, message);
}

/**
 * @brief 为账号创建角色
 * @param account_id 账号ID
 * @param char_name 角色名
 * @return 角色对象或0
 */
object create_character_for_account(string account_id, string char_name)
{
    object char_ob;

    if (!account_id || !char_name)
        return 0;

    char_ob = call_other("/daemons/game/char_d", "create_character", account_id, char_name, "男性");

    return char_ob;
}

/**
 * @brief 验证角色名
 * @param name 角色名
 * @return 1=合法, 0=非法
 */
int validate_name(string name)
{
    return call_other(LOGIN_AUTH, "valid_chinese_name", name);
}

/**
 * @brief 开始注册流程
 * @param username 用户名
 * @param password 密码
 * @param mode 模式
 */
int start_character_creation(string username, string password, string mode)
{
    int is_existing_account;
    object target;

    target = query_login_player() ? query_login_player() : this_object();
    if (query_reg_stage() != 0) {
        emit_login_feedback(target,
            "注册流程已启动，请勿重复操作。", "info");
        return 0;
    }

    is_existing_account = (mode == "existing");
    if (!is_existing_account && !CONFIG_D->query_registration_enabled()) {
        emit_login_feedback(target, "当前暂未开放新账号注册。", "error");
        return 0;
    }

    if (!is_existing_account && !call_other(PLAYER_D, "validate_password_format", password || "")) {
        emit_login_feedback(target,
            sprintf("密码长度必须为 %d-%d 个字符。", CONFIG_D->query_password_min_len(), ACCOUNT_MAX_PASSWORD_LENGTH),
            "error");
        return 0;
    }

    set_reg_stage(1);
    set_is_existing_account(is_existing_account);
    set_pending_username(username);
    set_pending_password(password || "");

    emit_registration_stage(this_object(), "name",
        sprintf("你是第一次来到这个世界。请输入你的角色名字（仅限中文，%d-%d个字）：",
            CHARACTER_MIN_NAME_LENGTH, CHARACTER_MAX_NAME_LENGTH));

    return 1;
}

/**
 * @brief 继续注册流程
 * @param input 用户输入
 */
void continue_registration(string input)
{
    string trimmed = trim_input(input);

    switch (query_reg_stage()) {
        case 1: /* 等待角色名 */
            if (process_registration_name(trimmed)) {
                /* 继续到性别阶段 */
            }
            break;
        case 2: /* 等待性别 */
            if (process_registration_gender(trimmed)) {
                finalize_character_creation();
            }
            break;
        default:
            break;
    }
}

/**
 * @brief 解析XK角色创建协议
 * @param data 数据映射
 * @return 1=成功, 0=失败
 */
int parse_xk_create(mapping data)
{
    string account, name, gender, password;
    int is_existing_account;

    if (!data || !mapp(data))
        return 0;

    account = data["account"];
    name = data["name"];
    gender = data["gender"];
    password = data["password"];

    if (!account || account == "")
        return 0;

    if (!call_other(LOGIN_AUTH, "valid_username", account))
        return 0;

    is_existing_account = call_other(PLAYER_D, "account_exists", account);
    if (!start_character_creation(account, password, is_existing_account ? "existing" : "manual"))
        return 0;

    if (stringp(name) && name != "") {
        if (!call_other(LOGIN_AUTH, "valid_chinese_name", name))
            return 0;
        process_registration_name(name);
    }

    if (stringp(gender) && gender != "") {
        if (!process_registration_gender(gender))
            return 0;
        return finalize_character_creation();
    }

    return 1;
}
