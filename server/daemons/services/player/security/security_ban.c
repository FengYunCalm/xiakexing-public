/**
 * @file    security_ban.c
 * @brief   安全监控 - IP 封禁管理子模块



 */

#pragma strict_types
#pragma save_types

#include <game_constants.h>

#ifndef _SECURITY_STATE_DEFINED
#error "This file should be included by security_monitor_d.c"
#endif

#define BAN_DURATION_SHORT      300     // 5分钟
#define BAN_DURATION_LONG       86400   // 24小时
#define BAN_DURATION_PERMANENT  0       // 永久

#define BAN_LIST_DIR            (DATA_DIR + "security/")
#define BAN_LIST_FILE           (BAN_LIST_DIR + "ban_list")

/**
 * @brief 检查IP是否被封禁
 * @param ip IP地址
 * @return 1=被封禁, 0=未被封禁
 */
int is_banned(string ip)
{
    mixed *ban_data;
    int current_time;

    if (!stringp(ip))
        return 0;

    ban_data = _banned_ips[ip];
    if (!arrayp(ban_data))
        return 0;

    current_time = time();
    if (ban_data[0] > 0 && current_time > ban_data[0])
    {
        map_delete(_banned_ips, ip);
        if (!save_ban_list())
            log_security("Expired ban removal could not be persisted");
        return 0;
    }

    return 1;
}

/**
 * @brief 封禁IP
 * @param ip IP地址
 * @param duration 封禁时长（秒，0=永久）
 * @param reason 封禁原因
 * @return 1=成功, 0=失败
 */
int ban_ip(string ip, int duration, string reason)
{
    int unban_time;

    if (!stringp(ip))
        return 0;

    if (duration <= 0)
        unban_time = 0;
    else
        unban_time = time() + duration;

    _banned_ips[ip] = ({
        unban_time,
        reason || "违规操作",
        time()  // 封禁时间
    });

    log_security(sprintf("IP banned: %s, duration: %d, reason: %s",
        ip, duration, reason || "违规操作"));

    // 本次会话内封禁仍然生效；落盘失败必须回报，不能让调用方以为已经持久化。
    if (!save_ban_list())
        return 0;

    return 1;
}

/**
 * @brief 解封IP
 * @param ip IP地址
 * @return 1=成功, 0=失败
 */
int unban_ip(string ip)
{
    if (!stringp(ip))
        return 0;

    if (undefinedp(_banned_ips[ip]))
        return 0;

    map_delete(_banned_ips, ip);

    log_security(sprintf("IP unbanned: %s", ip));

    if (!save_ban_list())
        return 0;

    return 1;
}

/**
 * @brief 清理过期封禁
  * @return 无返回值
 */
void cleanup_expired_bans()
{
    string *ips;
    string ip;
    mixed *ban_data;
    int current_time;
    int cleaned = 0;

    current_time = time();
    ips = keys(_banned_ips);

    foreach (ip in ips)
    {
        ban_data = _banned_ips[ip];

        if (ban_data[0] > 0 && current_time > ban_data[0])
        {
            map_delete(_banned_ips, ip);
            cleaned++;
        }
    }

    if (cleaned > 0)
    {
        log_info(sprintf("Cleaned %d expired IP bans", cleaned));
        if (!save_ban_list())
            log_security("Expired ban cleanup could not be persisted");
    }

    call_out("cleanup_expired_bans", 300);
}

/**
 * @brief 查询封禁列表
 * @return 封禁列表mapping
 */
mapping query_banned_ips()
{
    return copy(_banned_ips);
}

/**
 * @brief 保存封禁列表到文件
 * @private
 */
// @return 1=已落盘；0=拒绝写入或写入失败（调用方必须回报）
private int save_ban_list()
{
    string data;
    int written;

    if (!mapp(_banned_ips))
        _banned_ips = ([]);

    // 坏档：保留文件原样，不用当前空/半份名单覆盖，等待人工处理。
    if (_ban_list_load_failed) {
        log_security("Ban list save refused: previous load failed");
        return 0;
    }

    // 目录判定按驱动语义：file_size 对目录返回 -2、不存在返回 -1；
    // mkdir 返回 1 = 创建成功（本引擎约定非零为成功）。
    if (file_size(BAN_LIST_DIR) != -2) {
        if (mkdir(BAN_LIST_DIR) != 1 && file_size(BAN_LIST_DIR) != -2)
            return 0;
    }

    data = save_variable(_banned_ips);
    if (!stringp(data))
        return 0;

    written = write_file(BAN_LIST_FILE + ".o", data, 1);
    if (!written) {
        log_security("Ban list save failed");
        return 0;
    }

    return 1;
}

/**
 * @brief 从文件加载封禁列表
 * @private
 */
private int load_ban_list()
{
    int size;
    string data;
    mapping restored;

    size = file_size(BAN_LIST_FILE + ".o");
    if (size <= 0)
        return 1;   // 无文件 = 空名单，属正常初始状态

    data = read_file(BAN_LIST_FILE + ".o");
    if (!stringp(data) || data == "") {
        _ban_list_load_failed = 1;
        log_security(sprintf("Ban list unreadable (%d bytes); keeping state isolated", size));
        return 0;
    }

    restored = restore_variable(data);
    if (!mapp(restored)) {
        // 坏档不能悄悄变成空名单：置状态位并拒绝后续覆盖写。
        _ban_list_load_failed = 1;
        log_security(sprintf("Ban list corrupt (%d bytes); refusing to overwrite", size));
        return 0;
    }

    _ban_list_load_failed = 0;
    _banned_ips = restored;
    return 1;
}
