/**
 * @file    security_rate_limit.c
 * @brief   安全监控真源 - 速率限制子模块



 *
 * @par Description:
 * `SECURITY_MONITOR_D` 通过 include 本文件持有唯一限流状态与检查逻辑。
 */
#pragma strict_types
#pragma save_types

#ifndef _SECURITY_STATE_DEFINED
#error "This file should be included by security_monitor_d.c"
#endif

#define RATE_LIMIT_COMMANDS     50      // 每分钟最大命令数
#define RATE_LIMIT_MESSAGES     30      // 每分钟最大聊天消息
#define RATE_LIMIT_ATTACKS      20      // 每分钟最大攻击次数
#define RATE_LIMIT_LOGIN        5       // 每分钟最大登录尝试
#define RATE_LIMIT_LOADTEST_LOGIN 300   // 压测通道每分钟最大登录尝试

private int account_matches_prefix(string account, string prefix)
{
    if (!stringp(account) || !stringp(prefix) || prefix == "")
        return 0;

    return strsrch(account, prefix) == 0;
}

private int is_loadtest_account(string account, mapping cfg)
{
    mixed prefixes;
    string prefix;

    if (!mapp(cfg) || !cfg["enabled"])
        return 0;

    prefixes = cfg["account_prefixes"];
    if (stringp(prefixes))
        prefixes = ({ prefixes });
    if (!arrayp(prefixes))
        return 0;

    foreach (prefix in prefixes) {
        if (account_matches_prefix(account, prefix))
            return 1;
    }

    return 0;
}

private mapping query_loadtest_config()
{
    object config_d;

    config_d = find_object(CONFIG_D);
    if (!objectp(config_d))
        config_d = load_object(CONFIG_D);
    if (!objectp(config_d) || !function_exists("query_loadtest_config", config_d))
        return ([]);

    return config_d->query_loadtest_config();
}

/**
 * @brief 获取速率限制阈值
 * @param action_type 操作类型
 * @return 限制阈值
 */
private int get_rate_limit(string action_type)
{
    switch (action_type)
    {
        case "command":     return RATE_LIMIT_COMMANDS;
        case "message":     return RATE_LIMIT_MESSAGES;
        case "attack":      return RATE_LIMIT_ATTACKS;
        case "login":       return RATE_LIMIT_LOGIN;
        default:            return RATE_LIMIT_COMMANDS;
    }
}

/**
 * @brief 获取或创建计数器
 * @param key 计数器键
 * @param current_time 当前时间
 * @return 计数器数组
 */
private mixed *get_or_create_counter(string key, int current_time)
{
    mixed *counter = _rate_counters[key];

    if (!arrayp(counter))
    {
        // 新窗口从 0 开始，让本次请求只在下面累加一次（旧实现先置 1 再 ++，
        // 首个请求会被计成 2 次，边界次数判定偏严且不自洽）。
        counter = ({ 0, current_time + 60 });
        _rate_counters[key] = counter;
    }

    return counter;
}

/**
 * @brief 有界清理过期计数器
 *
 * 清理使用可重用的键快照分摊，避免每次限流请求完整扫描 mapping；
 * 当前请求的键另行即时淘汰，保证窗口边界不残留旧计数。
 */
private void prune_expired_rate_counters(int current_time)
{
    string key;
    mixed *counter;
    int inspected;

    if (!arrayp(_rate_counter_prune_keys)
        || _rate_counter_prune_index >= sizeof(_rate_counter_prune_keys)) {
        _rate_counter_prune_keys = keys(_rate_counters || ([]));
        _rate_counter_prune_index = 0;
    }
    while (_rate_counter_prune_index < sizeof(_rate_counter_prune_keys)
        && inspected++ < 16) {
        key = _rate_counter_prune_keys[_rate_counter_prune_index++];
        counter = _rate_counters[key];
        if (!arrayp(counter) || current_time > counter[1])
            map_delete(_rate_counters, key);
    }
}

/**
 * @brief 记录速率违规
 * @param identifier 标识符
 * @param action_type 操作类型
 */
private void record_rate_violation(string identifier, string action_type)
{
    log_security(sprintf("Rate limit exceeded: %s (%s)", identifier, action_type));
    record_violation(identifier, "rate_limit_" + action_type, 10);
}

private int check_rate_limit_with_limit(string identifier, string action_type, int limit, string counter_type)
{
    string key;
    mixed *counter;
    int current_time;

    if (!_monitoring_enabled)
        return 1;

    if (!stringp(identifier) || !stringp(action_type))
        return 1;

    if (limit <= 0)
        limit = get_rate_limit(action_type);

    key = identifier + "#" + counter_type;
    current_time = time();

    prune_expired_rate_counters(current_time);
    counter = _rate_counters[key];
    if (arrayp(counter) && current_time > counter[1])
        map_delete(_rate_counters, key);
    counter = get_or_create_counter(key, current_time);
    if (!arrayp(counter))
        return 1;

    counter[0]++;

    if (counter[0] > limit) {
        record_rate_violation(identifier, counter_type);
        return 0;
    }

    return 1;
}

/**
 * @brief 检查速率限制
 * @param identifier 标识符（IP或玩家ID）
 * @param action_type 操作类型（command/message/attack/login）
 * @return 1=允许, 0=超出限制
 */
int check_rate_limit(string identifier, string action_type)
{
    return check_rate_limit_with_limit(identifier, action_type, get_rate_limit(action_type), action_type);
}

int check_login_rate_limit(string identifier, string account)
{
    mapping cfg;
    int limit;

    /* 身份确认之前不授予管理员豁免：知道管理员账号名不等于拥有该账号，
     * 之前对 admins.cfg 名单直接放行会让管理员名字成为无限试密码的入口。
     * 运维自身登录同样走普通窗口，必要时用可信运维通道处理。 */

    cfg = query_loadtest_config();
    if (is_loadtest_account(account, cfg)) {
        limit = cfg["login_rate_limit"];
        if (limit <= 0)
            limit = RATE_LIMIT_LOADTEST_LOGIN;
        return check_rate_limit_with_limit(identifier, "login", limit, "login_loadtest");
    }

    return check_rate_limit(identifier, "login");
}

/**
 * @brief 重置速率计数器
 * @param identifier 标识符
 * @param action_type 操作类型
  * @return 无返回值
 */
void reset_rate_counter(string identifier, string action_type)
{
    string key;

    if (!stringp(identifier) || !stringp(action_type))
        return;

    key = identifier + "#" + action_type;
    map_delete(_rate_counters, key);
}

/**
 * @brief 获取速率计数器状态
 * @param identifier 标识符
 * @param action_type 操作类型
 * @return 当前计数
 */
int query_rate_counter(string identifier, string action_type)
{
    string key;
    mixed *counter;

    if (!stringp(identifier) || !stringp(action_type))
        return 0;

    key = identifier + "#" + action_type;
    counter = _rate_counters[key];

    if (!arrayp(counter))
        return 0;

    // 检查是否过期
    if (time() > counter[1])
        return 0;

    return counter[0];
}
