/**
 * @file    common_utils.h
 * @brief   公共工具函数头文件



 *
 * @par Description:
 * 消除代码重复的公共工具函数库。
 * 统一封装常用操作，提高代码可维护性。
 * 这些声明面向命令层和业务层的轻量辅助约定，返回值语义优先服务于
 * 早返回和用户提示，不替代更细粒度的业务错误分类。
 */

#pragma strict_types

#ifndef __LIB_INCLUDE_COMMON_UTILS_H__
#define __LIB_INCLUDE_COMMON_UTILS_H__

#include <system/ansi.h>

/**
 * @brief 检查对象是否有效
 * @param ob 对象
 * @param msg 失败时的提示消息(可选)
 * @return 1=有效, 0=无效
 * @note 若实现发送提示，目标通常是当前命令上下文，不代表对 ob 本身发消息。
 */
int check_object(object ob, string msg);

/**
 * @brief 检查玩家对象是否有效
 * @param me 玩家对象
 * @param cmd_name 命令名称(用于错误提示)
 * @return 1=有效, 0=无效
 * @note 这里的“玩家对象”偏向命令调用者语义，不保证兼容所有可交互角色对象。
 */
int check_player(object me, string cmd_name);

/**
 * @brief 检查两个对象是否都有效
 * @param ob1 对象1
 * @param ob2 对象2
 * @return 1=都有效, 0=有无效
 * @note 只回答有效性，不比较对象关系或环境一致性。
 */
int check_objects(object ob1, object ob2);

/**
 * @brief 发送成功消息(绿色)
 * @param me 目标对象
 * @param msg 消息内容
 * @note 颜色包装由实现决定，调用方应传纯业务文本。
 */
void msg_success(object me, string msg);

/**
 * @brief 发送错误消息(红色)
 * @param me 目标对象
 * @param msg 消息内容
 * @note 适合可继续交互的错误反馈，不等同于抛出异常或中断驱动流程。
 */


/**
 * @brief 发送警告消息(黄色)
 * @param me 目标对象
 * @param msg 消息内容
 * @note 用于非致命提醒，调用方仍需自行决定是否终止当前业务流程。
 */
void msg_warning(object me, string msg);

/**
 * @brief 发送信息消息(青色)
 * @param me 目标对象
 * @param msg 消息内容
 * @note 偏展示用途，不承诺被客户端归类为系统消息或协议消息。
 */
void msg_info(object me, string msg);

/**
 * @brief 发送高亮消息
 * @param me 目标对象
 * @param msg 消息内容
 * @note 高亮样式是表现层语义，具体颜色和包装可能随终端能力变化。
 */
void msg_highlight(object me, string msg);

/**
 * @brief 发送失败通知并返回0
 * @param msg 失败消息
 * @return 始终返回0
 * @note 适合直接写成 `return fail_msg(...)` 的早返回辅助。
 */
int fail_msg(string msg);

/**
 * @brief 打印标题栏
 * @param me 目标对象
 * @param title 标题文字
 */
void print_title(object me, string title);

/**
 * @brief 打印分隔线
 * @param me 目标对象
 */
void print_separator(object me);

/**
 * @brief 打印带样式的键值对
 * @param me 目标对象
 * @param key 键名
 * @param value 值
 * @param key_color 键的颜色(可选)
 * @param value_color 值的颜色(可选)
 * @note key/value 都按展示文本处理，函数不负责做数值格式化或空值兜底。
 */
void print_kv(object me, string key, string value, string key_color, string value_color);

/**
 * @brief 验证字符串非空
 * @param str 字符串
 * @param msg 错误消息(可选)
 * @return 1=非空, 0=空
 * @note “非空”通常指非 0 且去除常见空输入后可用，精确裁剪规则以实现为准。
 */
int validate_string(string str, string msg);

/**
 * @brief 验证数值范围
 * @param value 数值
 * @param min 最小值
 * @param max 最大值
 * @param msg 错误消息(可选)
 * @return 1=在范围内, 0=超出范围
 * @note 边界是否包含 min/max 由实现定义，调用方应与具体使用点保持一致。
 */
int validate_range(int value, int min_value, int max_value, string msg);

/**
 * @brief 验证玩家在线
 * @param player_id 玩家ID
 * @param msg 错误消息(可选)
 * @return 玩家对象或0
 * @note 返回的是当前在线对象引用快照，调用后仍需处理玩家下线或对象失效竞态。
 */
object validate_player_online(string player_id, string msg);

/**
 * @brief 格式化金钱显示
 * @param amount 金额(文)
 * @return 格式化后的字符串
 * @note 仅负责展示格式，不执行金额合法性校验或货币结算。
 */


/**
 * @brief 格式化百分比
 * @param value 当前值
 * @param max 最大值
 * @return 百分比字符串
 * @note max_value 的零值处理由实现决定，调用方不应依赖未记录的异常表现。
 */
string format_percent(int value, int max_value);

/**
 * @brief 格式化进度条
 * @param current 当前值
 * @param max 最大值
 * @param width 宽度(字符数)
 * @return 进度条字符串
 * @note 返回值只面向文本终端展示，宽度按字符数而不是可视像素计算。
 */
string format_progress_bar(int current, int max_value, int width);

#endif /* __LIB_INCLUDE_COMMON_UTILS_H__ */
