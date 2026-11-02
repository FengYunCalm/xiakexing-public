/**
 * @file    security_violation.c
 * @brief   安全监控 - 违规记录子模块



 */
#pragma strict_types
#pragma save_types

#ifndef _SECURITY_STATE_DEFINED
#error "This file should be included by security_monitor_d.c"
#endif

/**
 * @brief 记录违规行为
 * @param player_id 玩家ID
 * @param violation_type 违规类型
 * @param severity 严重程度（1-10）
  * @return 无返回值
 */
void record_violation(string identifier, string violation_type, int severity)
{
    mixed *violations;
    mapping violation;

    if (!stringp(identifier) || !stringp(violation_type))
        return;

    if (severity < 1) severity = 1;
    if (severity > 10) severity = 10;

    violation = ([
        "time": time(),
        "type": violation_type,
        "severity": severity
    ]);

    violations = _violation_history[identifier];
    if (!arrayp(violations))
        violations = ({});

    if (sizeof(violations) >= 50)
        violations = violations[1..49];

    violations += ({ violation });
    _violation_history[identifier] = violations;

    log_security(sprintf("Violation recorded: %s, type=%s, severity=%d",
        identifier, violation_type, severity));

    add_suspicious_score(identifier, severity * 5, "violation: " + violation_type);
}

/**
 * @brief 查询玩家违规历史
 * @param player_id 玩家ID
 * @param count 查询数量（0=全部）
 * @return 违规记录数组
 */
mixed *query_violation_history(string identifier, int count)
{
    mixed *violations;

    if (!stringp(identifier))
        return ({});

    violations = _violation_history[identifier];
    if (!arrayp(violations))
        return ({});

    if (count <= 0 || count >= sizeof(violations))
        return copy(violations);

    return copy(violations[sizeof(violations)-count..sizeof(violations)-1]);
}

/**
 * @brief 清除玩家违规记录
 * @param player_id 玩家ID
  * @return 无返回值
 */
void clear_violation_history(string identifier)
{
    if (!stringp(identifier))
        return;

    map_delete(_violation_history, identifier);
}

/**
 * @brief 获取违规统计
 * @param player_id 玩家ID
 * @return (["total": 总数, "severity": 总严重度])
 */
mapping query_violation_stats(string identifier)
{
    mixed *violations;
    mapping stats;
    mapping v;
    int total_severity;

    if (!stringp(identifier))
        return (["total": 0, "severity": 0]);

    violations = _violation_history[identifier];
    if (!arrayp(violations))
        return (["total": 0, "severity": 0]);

    total_severity = 0;
    foreach (v in violations)
    {
        total_severity += v["severity"];
    }

    stats = ([
        "total": sizeof(violations),
        "severity": total_severity
    ]);

    return stats;
}
