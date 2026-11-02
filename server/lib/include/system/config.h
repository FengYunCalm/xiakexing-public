/**
 * @file    config.h
 * @brief   配置管理契约头 - Configuration Management Contract



 *
 * @par Description:
 * 定义当前配置真源 `CONFIG_D` 的兼容访问宏与稳定配置键契约。
 * 本头文件只声明当前仓库仍在使用、且与 `/daemons/services/system/config/config_d.c`
 * 实际导出接口一致的访问方式；不要再把历史上不存在或已移除的接口声明回写进来。
 *
 * @par Usage:
 * @code
 * #include <system/config.h>
 * int shout_cost = CFG_GET_INT("game/shout_mp_cost", 10);
 * string start_room = CFG_GET_STRING("world/world/start_room", "");
 * @endcode
 *
 */

#pragma strict_types

#ifndef __SYSTEM_CONFIG_H__
#define __SYSTEM_CONFIG_H__

// 配置守护进程路径
/** 配置守护进程 */
#ifndef CONFIG_D
#define CONFIG_D            "/daemons/services/system/config/config_d"
#endif

// 配置访问宏
/**
 * @brief 获取配置值（通用）
 * @param key 配置键
 * @return 配置值
 */
#define CFG_GET(key)        (CONFIG_D->compat_get_config(key))

/**
 * @brief 获取整数配置
 * @param key 配置键
 * @param default_val 默认值
 * @return 配置值或默认值
 */
#define CFG_GET_INT(key, default_val)   (CONFIG_D->compat_get_config_int(key, default_val))

/**
 * @brief 获取字符串配置
 * @param key 配置键
 * @param default_val 默认值
 * @return 配置值或默认值
 */
#define CFG_GET_STRING(key, default_val) (CONFIG_D->compat_get_config_string(key, default_val))

/**
 * @brief 获取浮点数配置
 * @param key 配置键
 * @param default_val 默认值
 * @return 配置值或默认值
 */
#define CFG_GET_FLOAT(key, default_val) (CONFIG_D->compat_get_config_float(key, default_val))

/**
 * @brief 获取数组配置
 * @param key 配置键
 * @return 配置值或空数组
 */
#define CFG_GET_ARRAY(key)  (CONFIG_D->compat_get_config_array(key))

/**
 * @brief 获取映射配置
 * @param key 配置键
 * @return 配置值或空映射
 */
#define CFG_GET_MAP(key)    (CONFIG_D->compat_get_config_map(key))

/**
 * @brief 设置配置值
 * @param key 配置键
 * @param value 配置值
 */
#define CFG_SET(key, value) (CONFIG_D->compat_set_config(key, value))

/**
 * @brief 检查配置是否存在
 * @param key 配置键
 * @return 1=存在, 0=不存在
 */
#define CFG_EXISTS(key)     (CONFIG_D->compat_config_exists(key))

/**
 * @brief 删除配置
 * @param key 配置键
 */
#define CFG_DELETE(key)     (CONFIG_D->compat_delete_config(key))

// 当前仓库仍可追溯到真实消费链的兼容配置键。
// 其他历史 key 常量已移除，避免把“看起来像稳定 API、实际没人读”的假契约继续扩散。
#define CFG_SERVER_NAME                 "server/name"
#define CFG_SERVER_VERSION              "server/version"
#define CFG_SERVER_DEBUG_MODE           "server/debug_mode"
#define CFG_PLAYER_START_HP             "player/start_hp"
#define CFG_PLAYER_START_MP             "player/start_mp"
#define CFG_PLAYER_MAX_HP               "player/max_hp"
#define CFG_PLAYER_MAX_MP               "player/max_mp"
#define CFG_PLAYER_INVENTORY_SIZE       "player/inventory_size"
#define CFG_SECURITY_MAX_LOGIN_RETRY    "security/max_login_retry"
#define CFG_SECURITY_LOCK_TIME          "security/lock_time"
#define CFG_SECURITY_PASSWORD_MIN_LEN   "security/password_min_len"
#define CFG_SECURITY_SESSION_TIMEOUT    "security/session_timeout"
#define CFG_LOG_LEVEL                   "log/level"
#define CFG_LOG_MAX_SIZE                "log/max_size"
#define CFG_LOG_RETENTION_DAYS          "log/retention_days"
#define CFG_CHAT_SHOUT_MP_COST          "chat/shout_mp_cost"
#define CFG_CHAT_WHO_PAGE_SIZE          "chat/who_page_size"
#define CFG_WORLD_START_ROOM            "world/start_room"
#define CFG_WORLD_ROOT_PATHS            "world/root_paths"

// 配置变更回调
/**
 * @brief 注册配置变更回调
 * @param key 配置键（支持通配符 *）
 * @param callback 回调函数名
 * @param owner 回调对象
 */
#define CFG_ON_CHANGE(key, callback, owner) \
    (CONFIG_D->compat_register_config_callback(key, callback, owner))

/**
 * @brief 注销配置变更回调
 * @param key 配置键
 * @param owner 回调对象
 */
#define CFG_OFF_CHANGE(key, owner) \
    (CONFIG_D->compat_unregister_config_callback(key, owner))

// 配置热重载
/**
 * @brief 重载指定已注册配置名
 * @param config_name 已注册配置名，如 game/combat/world
 */
#define CFG_RELOAD(config_name) \
    (CONFIG_D->reload_config(config_name))

// 默认配置值
/** @name 默认配置值常量 */
/** @{ */
#define DEFAULT_PLAYER_START_HP     100
#define DEFAULT_PLAYER_START_MP     50
#define DEFAULT_PLAYER_MAX_HP       1000
#define DEFAULT_PLAYER_MAX_MP       500
#define DEFAULT_COMBAT_INTERVAL     3
#define DEFAULT_COMBAT_MAX_ROUNDS   100
#define DEFAULT_COMBAT_BASE_HIT     80
#define DEFAULT_SKILL_MAX_LEVEL     1000
#define DEFAULT_MAX_LOGIN_RETRY     5
#define DEFAULT_LOCK_TIME           1800
/** @} */

#endif /* __SYSTEM_CONFIG_H__ */
