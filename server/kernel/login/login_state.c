/**
 * @file    login_state.c
 * @brief   登录状态管理
 * @author  XiaKeXing Development Team
 * @date    2026-03-20
 * @version 2.1.1
 *
 * @par Description:
 * 管理登录状态与注册流程状态机，从 `login.c` 拆分而来。
 */

#pragma strict_types

#include <system/globals.h>
#include <system/daemon.h>
#include <system/xk_protocol.h>
#include <system/ansi.h>
#include <framework/message_facade.h>

#define REG_STAGE_NONE      0
#define REG_STAGE_NAME      1
#define REG_STAGE_GENDER    2
#define REG_STAGE_CREATE    3

#define LOGIN_AUTH      "/kernel/login/login_auth"
#define LOGIN_SESSION   "/kernel/login/login_session"

nosave object _player_ob;
nosave int _is_logged_in;
nosave int _reg_stage;
nosave string _pending_username;
nosave string _pending_password;
nosave string _pending_name;
nosave string _pending_gender;
nosave object _login_player;
nosave int _is_existing_account;
nosave mapping _temp_data;

/**
 * @brief 初始化登录状态
 */
void init_login_state()
{
    _is_logged_in = 0;
    _reg_stage = REG_STAGE_NONE;
    _pending_username = "";
    _pending_password = "";
    _pending_name = "";
    _pending_gender = "";
    _is_existing_account = 0;
    _temp_data = ([]);
}

void set_temp(string key, mixed value)
{
    if (!stringp(key) || key == "")
        return;
    if (!mapp(_temp_data))
        _temp_data = ([]);
    _temp_data[key] = value;
}

mixed query_temp(string key)
{
    if (!stringp(key) || key == "" || !mapp(_temp_data))
        return 0;
    return _temp_data[key];
}

void delete_temp(string key)
{
    if (!stringp(key) || key == "" || !mapp(_temp_data))
        return;
    map_delete(_temp_data, key);
}

/**
 * @brief 清理注册状态并重置变量
 */
void reset_registration_state()
{
    _reg_stage = REG_STAGE_NONE;
    _pending_username = 0;
    _pending_password = 0;
    _pending_name = 0;
    _pending_gender = 0;
    _is_existing_account = 0;

    if (objectp(_player_ob)) {
        destruct(_player_ob);
        _player_ob = 0;
    }
}

/**
 * @brief 获取当前注册阶段
 */
int query_reg_stage() { return _reg_stage; }

/**
 * @brief 设置注册阶段
 */
void set_reg_stage(int stage) { _reg_stage = stage; }

/**
 * @brief 获取登录状态
 */
int query_is_logged_in() { return _is_logged_in; }

/**
 * @brief 设置登录状态
 */
void set_is_logged_in(int status) { _is_logged_in = status; }

/**
 * @brief 获取待处理用户名
 */
string query_pending_username() { return _pending_username; }

/**
 * @brief 设置待处理用户名
 */
void set_pending_username(string name) { _pending_username = name; }

string query_pending_password() { return _pending_password; }

void set_pending_password(string password) { _pending_password = password; }

string query_pending_name() { return _pending_name; }

void set_pending_name(string name) { _pending_name = name; }

string query_pending_gender() { return _pending_gender; }

void set_pending_gender(string gender) { _pending_gender = gender; }

int query_is_existing_account() { return _is_existing_account; }

void set_is_existing_account(int flag) { _is_existing_account = flag ? 1 : 0; }

/**
 * @brief 获取玩家对象
 */
object query_player_ob() { return _player_ob; }

/**
 * @brief 设置玩家对象
 */
void set_player_ob(object ob) { _player_ob = ob; }

/**
 * @brief 获取登录玩家对象
 */
object query_login_player() { return _login_player; }

/**
 * @brief 设置登录玩家对象
 */
void set_login_player(object ob) { _login_player = ob; }
private object current_login_recipient();
mapping restore_login_projection_for_login(object actor);

private void inherit_login_protocol_flags(object player)
{
    string xk_protocol_version;

    if (!objectp(player))
        return;

    xk_protocol_version = query_temp("xk_protocol_version");
    if (stringp(xk_protocol_version) && xk_protocol_version != "") {
        player->delete_temp("client/plain_text_only");
        player->set_temp("xk_protocol_version", xk_protocol_version);
        return;
    }

    if (query_temp("client/plain_text_only"))
        player->set_temp("client/plain_text_only", 1);
}

/**
 * @brief 解析性别输入
 * @param input 输入字符串
 * @return 标准化性别字符串或0
 */
string parse_gender_input(string input)
{
    if (!input || input == "")
        return 0;

    input = lower_case(input);

    if (input == "男" || input == "male" || input == "m" || input == "1")
        return "男性";

    if (input == "女" || input == "female" || input == "f" || input == "0")
        return "女性";

    return 0;
}

/**
 * @brief 处理注册取消命令
 */
void handle_registration_cancel()
{
    emit_login_feedback(current_login_recipient(),
        "注册已取消。", "info");
    reset_registration_state();
}

/**
 * @brief 处理角色名输入阶段
 * @param trimmed_input 修剪后的输入
 * @return 1=继续下一步, 0=保持当前阶段
 */
int process_character_name_input(string trimmed_input)
{
    if (!trimmed_input || trimmed_input == "") {
        _pending_gender = (random(2) == 0) ? "男性" : "女性";
        _pending_name = call_other(NAME_D, "generate_random_name", _pending_gender);
        emit_login_feedback(current_login_recipient(),
            "已为你随机生成角色名：" + _pending_name, "info");
    } else if (!call_other(LOGIN_AUTH, "valid_chinese_name", trimmed_input)) {
        emit_registration_stage(current_login_recipient(), "name",
            sprintf("名字必须是%d-%d个汉字，不能包含英文字母或数字，请重新输入（或直接回车使用随机名字）：",
                CHARACTER_MIN_NAME_LENGTH, CHARACTER_MAX_NAME_LENGTH));
        return 0;
    } else {
        _pending_name = trimmed_input;
    }

    _reg_stage = REG_STAGE_GENDER;
    emit_registration_stage(current_login_recipient(), "gender", "请选择你的性别（男/女）：");
    return 1;
}

/**
 * @brief 处理性别选择阶段
 * @param trimmed_input 修剪后的输入
 * @return 1=继续下一步, 0=保持当前阶段
 */
int process_character_gender_input(string trimmed_input)
{
    string gender = parse_gender_input(trimmed_input);

    if (!gender) {
        emit_registration_stage(current_login_recipient(), "gender", "请输入\"男\"或\"女\"：");
        return 0;
    }

    _pending_gender = gender;
    _reg_stage = REG_STAGE_CREATE;
    return 1;
}

private int finalize_pending_character_creation(int is_existing_account)
{
    mapping phase_started_at;
    mapping started_at;
    mapping projection_restore;
    mixed projection_err;
    string session_id;

    started_at = call_other(LOGIN_SESSION, "begin_loadtest_login_phase_timing");
    phase_started_at = call_other(LOGIN_SESSION, "begin_loadtest_login_phase_timing");

    _player_ob = call_other(LOGIN_SESSION, "do_create_character",
        _pending_username, _pending_password, _pending_name, _pending_gender,
        is_existing_account, this_object());
    call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
        "character_persist", phase_started_at, objectp(_player_ob));

    if (!_player_ob) {
        _reg_stage = REG_STAGE_NAME;
        call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
            "character_create", started_at, 0);
        return 0;
    }

    phase_started_at = call_other(LOGIN_SESSION, "begin_loadtest_login_phase_timing");
    if (function_exists("setup", _player_ob))
        _player_ob->setup();
    inherit_login_protocol_flags(_player_ob);

    _player_ob->set_temp("login/bootstrap_room_sync", 1);
    projection_err = catch(
        projection_restore = restore_login_projection_for_login(_player_ob));
    if (projection_err || !mapp(projection_restore) || !projection_restore["ok"]
        || !objectp(environment(_player_ob))) {
        log_file("system/session", sprintf(
            "[%s] finalize_pending_character_creation: projection placement failed for %s: %O\n",
            ctime(time()), _pending_username || "<unknown>",
            projection_err || projection_restore));
        session_id = _player_ob->query_temp("session_id");
        if (stringp(session_id) && session_id != "")
            call_other(LOGIN_SESSION, "destroy_session", session_id);
        destruct(_player_ob);
        _player_ob = 0;
        _reg_stage = REG_STAGE_NAME;
        call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
            "character_setup_move", phase_started_at, 0);
        call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
            "character_create", started_at, 0);
        return 0;
    }

    _pending_username = "";
    _pending_password = "";
    _pending_name = "";
    _pending_gender = "";
    _reg_stage = REG_STAGE_NONE;
    _is_existing_account = is_existing_account;

    call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
        "character_setup_move", phase_started_at, 1);
    call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
        "character_create", started_at, 1);
    return 1;
}

/**
 * @brief 完成角色创建并进入游戏
 * @return 1=成功, 0=失败
 */
int finalize_character_creation()
{
    /*
     * 文本注册与 XKCREATE 都应先把 pending 字段准备齐，再统一走这里的建角完成链。
     * 连接切换、房间同步和成功提示继续统一由 finish_login_transition() 收口。
     */
    return finalize_pending_character_creation(_is_existing_account);
}

int finalize_character_creation_for_account(int is_existing_account)
{
    return finalize_pending_character_creation(is_existing_account);
}

mapping restore_login_projection_for_login(object actor)
{
    return LOGIN_PROJECTION_D->restore_login_projection(actor);
}
