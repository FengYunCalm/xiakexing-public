/**
 * @file    security_suspicious.c
 * @brief   安全监控 - 可疑行为检测子模块



 */
#pragma strict_types
#pragma save_types

#include <game_constants.h>

#ifndef _SECURITY_STATE_DEFINED
#error "This file should be included by security_monitor_d.c"
#endif

#define SUSPICIOUS_SCORE_WARN   50
#define SUSPICIOUS_SCORE_BAN    100
#define SUSPICIOUS_ACTIVITY_BAN_DURATION 3600

/**
 * @brief 增加可疑分数
 * @param player_id 玩家ID
 * @param score 分数
 * @param reason 原因
  * @return 无返回值
 */
void add_suspicious_score(string identifier, int score, string reason)
{
    int current_score;

    if (!stringp(identifier))
        return;

    current_score = _suspicious_players[identifier] || 0;
    current_score += score;
    _suspicious_players[identifier] = current_score;

    log_security(sprintf("Suspicious score: %s +%d (%s), total=%d",
        identifier, score, reason || "", current_score));

    if (current_score >= SUSPICIOUS_SCORE_BAN)
    {
        log_security(sprintf("Auto-ban threshold reached: %s", identifier));
    }
    else if (current_score >= SUSPICIOUS_SCORE_WARN)
    {
        log_security(sprintf("Warning threshold reached: %s", identifier));
    }
}

/**
 * @brief 减少可疑分数（随时间衰减）
 * @param player_id 玩家ID
 * @param score 分数
  * @return 无返回值
 */
void reduce_suspicious_score(string identifier, int score)
{
    int current_score;

    if (!stringp(identifier) || score <= 0)
        return;

    current_score = _suspicious_players[identifier] || 0;
    current_score -= score;

    if (current_score <= 0)
        map_delete(_suspicious_players, identifier);
    else
        _suspicious_players[identifier] = current_score;
}

/**
 * @brief 检查可疑活动
 * @param player 玩家对象
 * @param action 动作
 * @return 1=正常, 0=可疑
 */
int check_suspicious_activity(object player, string action)
{
    string player_id;
    string ip;
    int score;
    int current_score;

    if (!objectp(player) || !stringp(action))
        return 1;

    player_id = get_object_id(player);
    if (!stringp(player_id))
        return 1;

    score = 0;

    switch (action)
    {
        case "rapid_commands":
            score = 20;
            break;
        case "mass_messaging":
            score = 15;
            break;
        case "failed_logins":
            score = 30;
            break;
        case "abnormal_movement":
            score = 25;
            break;
        default:
            score = 5;
    }

    if (score > 0)
        add_suspicious_score(player_id, score, action);

    current_score = _suspicious_players[player_id] || 0;
    if (current_score >= SUSPICIOUS_SCORE_BAN) {
        ip = query_ip_number(player);
        if (stringp(ip) && ip != "" && !is_banned(ip))
            ban_ip(ip, SUSPICIOUS_ACTIVITY_BAN_DURATION, "suspicious_activity:" + action);
        return 0;
    }

    return 1;
}

/**
 * @brief 查询可疑玩家列表
 * @return 可疑玩家mapping
 */
mapping query_suspicious_players()
{
    return copy(_suspicious_players);
}

/**
 * @brief 清除玩家可疑分数
 * @param player_id 玩家ID
  * @return 无返回值
 */
void clear_suspicious_score(string identifier)
{
    if (!stringp(identifier))
        return;

    map_delete(_suspicious_players, identifier);
}
