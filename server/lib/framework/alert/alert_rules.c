/**
 * @file    alert_rules.c
 * @brief   告警规则子模块



 *
 * @par Description:
 * 告警系统内部子模块。
 * 兼容 forwarding shell，规则数据与判定语义都由 alert_system.c 持有。
 */

#pragma strict_types
#pragma save_types

#define ALERT_SYSTEM "/lib/framework/alert_system"

int set_alert_threshold(string metric_name, float threshold, int level, string message, int cooldown)
{
    /* 保留旧模块入口，实际注册仍写入 alert_system 真源状态。 */
    return call_other(ALERT_SYSTEM, "set_alert_threshold", metric_name, threshold, level, message, cooldown);
}

int remove_alert_rule(string metric_name)
{
    return call_other(ALERT_SYSTEM, "remove_alert_rule", metric_name);
}

mixed *get_alert_rules(string metric_name)
{
    /* canonical owner 返回独立快照，壳层不持有规则状态。 */
    return call_other(ALERT_SYSTEM, "get_alert_rules", metric_name);
}
