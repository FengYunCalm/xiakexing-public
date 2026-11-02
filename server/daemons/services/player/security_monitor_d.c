/**
 * @file    security_monitor_d.c
 * @brief   安全监控服务门面



 *
 * @par Description:
 * 安全监控服务门面，负责协调速率限制、封禁、可疑行为检测和违规记录子模块。
 */
#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/daemon.h>
#include <system/errors.h>
#include <framework/message_facade.h>

inherit "/lib/framework/daemon";

#define _SECURITY_STATE_DEFINED

// This file is the facade and state owner for the security service boundary.
// Included subfiles split rate-limit, ban, suspicious, and violation logic under one daemon API.
nosave mapping _rate_counters;
nosave string *_rate_counter_prune_keys;
nosave int _rate_counter_prune_index;
nosave mapping _banned_ips;
// 载入 ban_list 失败时置位：保留可区分状态，禁止把空名单写回覆盖原文件。
nosave int _ban_list_load_failed;
nosave mapping _suspicious_players;
nosave mapping _violation_history;

#define SECURITY_LOG_PREFIX     "[SECURITY_D]"

private nosave int _monitoring_enabled = 1;

void load_ban_list();
int save_ban_list();
void record_violation(string player_id, string violation_type, int severity);

void log_info(string msg)
{
    log_file("system/security", sprintf("[INFO] %s\n", msg));
}

void log_security(string msg)
{
    log_file("system/security", sprintf("[SECURITY] %s\n", msg));
}

#include "security/security_rate_limit.c"
#include "security/security_ban.c"
#include "security/security_suspicious.c"
#include "security/security_violation.c"

void create()
{
    ::create();

    _rate_counters = ([]);
    _rate_counter_prune_keys = ({ });
    _rate_counter_prune_index = 0;
    _banned_ips = ([]);
    _ban_list_load_failed = 0;
    _suspicious_players = ([]);
    _violation_history = ([]);
    _monitoring_enabled = 1;

    load_ban_list();

    call_out("cleanup_expired_bans", 300);

    log_info("Security monitor service initialized (Facade v2.0)");
}

/**
 * @brief 设置监控开关
 * @param flag 1=开启, 0=关闭
  * @return 无返回值
 */
void enable_monitoring(int flag)
{
    _monitoring_enabled = flag ? 1 : 0;
    log_info(sprintf("Security monitoring %s", _monitoring_enabled ? "enabled" : "disabled"));
}
