/**
 * @file    login.h
 * @brief   登录系统内部共享参考头
 * @author  XiaKeXing Development Team
 * @date    2026-03-11
 * @version 2.1.0
 *
 * @par Description:
 * 当前文件只服务于 login 门面拆分文件之间的内部共享声明。
 * 它不是“完整且稳定的登录公共 API”；真实跨文件调用面必须以当前实现为准同步维护。
 */

#pragma strict_types

#ifndef __KERNEL_LOGIN_LOGIN_H__
#define __KERNEL_LOGIN_LOGIN_H__

#include <system/account.h>

/* 常量定义
 * ============================================================ */

#ifndef REG_STAGE_NONE
#define REG_STAGE_NONE      0
#define REG_STAGE_NAME      1
#define REG_STAGE_GENDER    2
#define REG_STAGE_CREATE    3
#endif

#ifndef MAX_INPUT_LENGTH
#define MAX_INPUT_LENGTH    256
#endif

/* login_state.c - 登录状态管理
 * ============================================================ */

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
string query_pending_password();
void set_pending_password(string password);
string query_pending_name();
void set_pending_name(string name);
string query_pending_gender();
void set_pending_gender(string gender);
object query_player_ob();
void set_player_ob(object ob);
object query_login_player();
void set_login_player(object ob);
string parse_gender_input(string input);
void handle_registration_cancel();
int process_registration_name(string trimmed_input);
int process_registration_gender(string trimmed_input);
int finalize_character_creation();
int finalize_character_creation_for_account(int is_existing_account);

/* login_input.c - 登录输入处理
 * ============================================================ */

string trim_input(string input);
int is_cancel_command(string input);
string process_input(string input);
string handle_xk_protocol(string str, object login_ob);

#endif /* __KERNEL_LOGIN_LOGIN_H__ */
