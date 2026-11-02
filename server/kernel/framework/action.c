/**
 * @file    action.c
 * @brief   动作控制框架
 * @author  XiaKeXing Development Team
 * @date    2026-03-20
 * @version 1.0.1
 *
 * @par Description:
 * 提供 busy 状态管理、动作队列控制与中断处理。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/constants.h>
#include <system/ansi.h>
#include <framework/message_facade.h>

mixed query_temp(string path);
int set_temp(string path, mixed value);
int delete_temp(string path);

private int query_xk_client()
{
    mixed version;

    version = query_temp("xk_protocol_version");
    return stringp(version) && version != "";
}

private void cancel_busy_countdown()
{
    int handle;

    handle = query_temp("action/busy_countdown_handle");
    if (intp(handle) && handle > 0)
        remove_call_out(handle);
    delete_temp("action/busy_countdown_handle");
    delete_temp("action/busy_countdown_until");
}

private void schedule_busy_countdown(int busy_until)
{
    int handle;

    cancel_busy_countdown();
    if (!intp(busy_until) || busy_until <= time() || !userp(this_object()))
        return;

    set_temp("action/busy_countdown_until", busy_until);
    handle = call_out("emit_busy_countdown", 0);
    set_temp("action/busy_countdown_handle", handle);
}

void emit_busy_countdown()
{
    int busy_until;
    int handle;
    int remaining;

    if (!userp(this_object())) {
        cancel_busy_countdown();
        return;
    }

    busy_until = query_temp("action/busy");
    if (!intp(busy_until) || busy_until <= time()) {
        delete_temp("action/busy");
        cancel_busy_countdown();
        return;
    }

    remaining = busy_until - time();
    if (remaining <= 0) {
        delete_temp("action/busy");
        cancel_busy_countdown();
        return;
    }

    if (!query_xk_client())
        msg_send(this_object(), sprintf(HIY "busy %d：你正忙着，暂时无法行动。\n" NOR, remaining));
    if (remaining <= 1) {
        delete_temp("action/busy_countdown_handle");
        delete_temp("action/busy_countdown_until");
        return;
    }

    handle = call_out("emit_busy_countdown", 1);
    set_temp("action/busy_countdown_handle", handle);
}

/**
 * @brief 检查对象是否处于busy状态
 * @return 1表示忙，0表示空闲
 */
int is_busy()
{
    int busy_time;
    busy_time = query_temp("action/busy");

    if (!intp(busy_time))
        return 0;

    if (busy_time > 0 && busy_time <= time()) {
        delete_temp("action/busy");
        cancel_busy_countdown();
        return 0;
    }

    return (busy_time > 0);
}

/**
 * @brief 设置busy状态
 * @param duration busy持续时间（秒）
 * @return 无返回值
 */
void start_busy(int duration)
{
    int busy_until;

    if (!intp(duration) || duration <= 0)
        return;

    busy_until = time() + duration;
    set_temp("action/busy", busy_until);
    schedule_busy_countdown(busy_until);
}

/**
 * @brief 增加busy时间
 * @param duration 增加的busy时间
 * @return 无返回值
 */
void add_busy(int duration)
{
    int current_busy;

    if (!intp(duration) || duration <= 0)
        return;

    current_busy = query_temp("action/busy");
    if (!intp(current_busy) || current_busy < time())
        current_busy = time();

    current_busy += duration;
    set_temp("action/busy", current_busy);
    schedule_busy_countdown(current_busy);
}

/**
 * @brief 清除busy状态
 * @return 无返回值
 */
void interrupt_busy()
{
    delete_temp("action/busy");
    cancel_busy_countdown();
    delete_temp("action/action_queue");
}

/**
 * @brief 查询剩余busy时间
 * @return 剩余busy秒数
 */
int query_busy_left()
{
    int busy_until;
    int remaining;

    busy_until = query_temp("action/busy");

    if (!intp(busy_until))
        return 0;

    if (busy_until <= time()) {
        delete_temp("action/busy");
        cancel_busy_countdown();
        return 0;
    }

    remaining = busy_until - time();
    return (remaining > 0) ? remaining : 1;
}

/**
 * @brief 添加动作到队列
 * @param action 动作函数名
 * @param delay 延迟执行的心跳数
 * @param args 动作参数数组
 * @return 1成功，0失败
 */
int queue_action(string action, int delay, mixed *args)
{
    mapping *queue;
    mapping new_action;

    if (!stringp(action) || action == "")
        return 0;

    if (!intp(delay))
        delay = 0;

    if (!arrayp(args))
        args = ({});

    queue = query_temp("action/action_queue");
    if (!arrayp(queue))
        queue = ({});

    new_action = ([
        "action": action,
        "delay": delay,
        "args": args,
        "timestamp": time()
    ]);

    queue += ({ new_action });
    set_temp("action/action_queue", queue);

    return 1;
}

/**
 * @brief 获取动作队列
 * @return 动作队列数组
 */
mixed *query_action_queue()
{
    mixed *queue;
    queue = query_temp("action/action_queue");
    return arrayp(queue) ? queue : ({});
}

/**
 * @brief 清空动作队列
 * @return 无返回值
 */
void clear_action_queue()
{
    delete_temp("action/action_queue");
}

/**
 * @brief 执行队列中的下一个动作
 * @return 1执行了动作，0队列为空
 */
int execute_next_action()
{
    while (1) {
        mixed *queue = query_temp("action/action_queue");
        if (!arrayp(queue) || sizeof(queue) == 0)
            return 0;

        mapping action_data = queue[0];
        queue = queue[1..];
        set_temp("action/action_queue", queue);

        if (!mapp(action_data)) continue;

        string action = action_data["action"];
        if (!stringp(action)) continue;

        mixed *args = action_data["args"];
        if (arrayp(args) && sizeof(args) > 0) {
            call_other(this_object(), action, args...);
        } else {
            call_other(this_object(), action);
        }
        return 1;
    }
}

// 动作限制检查
/**
 * @brief 检查是否可以执行动作
 * @param show_msg 是否显示busy消息
 * @return 1可以执行，0不可以
 */
int can_perform_action(int show_msg)
{
    if (is_busy()) {
        if (show_msg) {
            msg_send(this_object(), HIY "你正忙着呢。\n" NOR);
        }
        return 0;
    }
    return 1;
}

/**
 * @brief 检查并执行动作，如果busy则加入队列
 * @param action 动作函数名
 * @param delay 延迟
 * @param args 参数
 * @return 1立即执行或加入队列，0失败
 */
int try_perform_action(string action, int delay, mixed *args)
{
    if (!stringp(action) || action == "")
        return 0;

    if (is_busy()) {
        // busy中，加入队列
        return queue_action(action, delay, args);
    }

    // 不busy，立即执行
    if (arrayp(args) && sizeof(args) > 0) {
        call_other(this_object(), action, args...);
    } else {
        call_other(this_object(), action);
    }

    return 1;
}

// 心跳处理
/**
 * @brief 心跳函数，处理动作队列
 * @return 无返回值
 */
void heart_beat_action()
{
    // 检查busy是否过期
    if (is_busy())
        return;

    // 执行队列中的动作
    execute_next_action();
}

// 创建和清理
/**
 * @brief 重置动作状态
 * @return 无返回值
 */
void reset_action_state()
{
    interrupt_busy();
    clear_action_queue();
}
