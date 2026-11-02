/**
 * @file    move.c
 * @brief   移动控制框架
 * @author  XiaKeXing Development Team
 * @date    2026-03-20
 * @version 1.0.1
 *
 * @par Description:
 * 提供对象移动、环境检查、移动限制和事件回调。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/ansi.h>
#include <framework/message_facade.h>

int can_move(object dest);
int on_can_move(object dest);
void on_leave(object old_env);
void on_arrive(object dest);
void on_move_complete(object old_env, object dest);

private string room_short_name(object room);
private string short_direction_name(string dir);
private string opposite_direction(string dir);
private string find_exit_direction(object from_room, object to_room);
private mapping movement_room_message_options(object room);
private int is_room_environment(object env);
private void touch_room_view_content_version(object room);
private void mark_room_view_transition(object old_env, object new_env);
private void record_loadtest_move_trace(object me, object old_env, object new_env);

/*
 * Item placement is a hot lifecycle path (buy, rewards and stack repair).
 * `log_file()` is a synchronous filesystem boundary, while move_debug has no
 * gameplay consumer for successful movement. Retain failure diagnostics for
 * characters without making every successful movement perform file I/O.
 */
private int should_log_move_debug(object me)
{
    if (!objectp(me))
        return 0;
    if (living(me))
        return 1;

    return function_exists("is_character", me) && me->is_character();
}

private int can_destination_receive_object(object obj, object dest)
{
    int item_weight;
    int current_encumbrance;
    int max_encumbrance;

    if (!objectp(obj) || !objectp(dest))
        return 0;

    if (function_exists("can_hold", dest))
        return dest->can_hold(obj);

    if (function_exists("is_character", dest) && dest->is_character()) {
        item_weight = obj->query_weight() || 1;
        current_encumbrance = dest->query_encumbrance();
        max_encumbrance = dest->query_max_encumbrance();
        return (current_encumbrance + item_weight) <= max_encumbrance;
    }

    return 1;
}

private int is_room_environment(object env)
{
    if (!objectp(env))
        return 0;

    return function_exists("ensure_room_entities", env)
        || function_exists("query_exits", env)
        || mapp(env->query("exits"));
}

private void touch_room_view_content_version(object room)
{
    mixed version;

    if (!is_room_environment(room))
        return;

    version = room->query_temp("room_view/content_version");
    if (!intp(version) || version < 0)
        version = 0;

    room->set_temp("room_view/content_version", version + 1);
    room->set_temp("room_view/content_updated_at", time());
}

private void mark_room_view_transition(object old_env, object new_env)
{
    if (objectp(old_env))
        touch_room_view_content_version(old_env);
    if (objectp(new_env))
        touch_room_view_content_version(new_env);
}

/* Fixture-only causal evidence; never changes movement or normal protocol state. */
private void record_loadtest_move_trace(object me, object old_env, object new_env)
{
    mixed raw_trace;
    mapping *trace;
    mapping entry;
    string run_id;
    string command;

    if (!objectp(me) || !function_exists("query_temp", me))
        return;

    run_id = me->query_temp("loadtest/move_trace_run_id");
    if (!stringp(run_id) || run_id == "")
        return;

    raw_trace = me->query_temp("loadtest/move_trace");
    trace = arrayp(raw_trace) ? raw_trace : ({ });
    command = me->query_temp("loadtest/active_command");
    entry = ([
        "from": objectp(old_env) ? base_name(old_env) : "",
        "to": objectp(new_env) ? base_name(new_env) : "",
        "command": stringp(command) ? command : "",
    ]);
    trace += ({ entry });
    if (sizeof(trace) > 8)
        trace = trace[<8..];
    me->set_temp("loadtest/move_trace", trace);
}

/**
 * @brief 获取当前所在环境
 * @return 环境对象，无环境返回0
 */
object query_environment()
{
    return environment(this_object());
}

/**
 * @brief 获取环境中的所有对象
 * @return 对象数组
 */
object *query_inventory()
{
    object env;
    env = environment(this_object());
    if (objectp(env))
        return all_inventory(env);
    return ({});
}

/**
 * @brief 检查是否在指定环境中
 * @param ob 要检查的环境对象
 * @return 1在环境中，0不在
 */
int is_in_environment(object ob)
{
    object env;

    if (!objectp(ob))
        return 0;

    env = environment(this_object());
    return (env == ob);
}

/**
 * @brief 移动到指定环境
 * @param dest 目标环境对象
 * @param silent 是否静默移动（不触发消息）
 * @return 1成功，0失败
 */
int move_to(object dest, int silent)
{
    object me;
    object old_env;
    string leave_dir;
    string arrive_dir;

    me = this_object();

    if (!objectp(dest))
        return 0;

    if (!can_destination_receive_object(me, dest))
        return 0;

    old_env = environment(me);

    if (old_env == dest)
        return 1;

    if (!can_move(dest))
    {
        if (should_log_move_debug(me))
            log_file("system/move_debug", sprintf("can_move blocked obj=%O dest=%O\n", me, dest));
        return 0;
    }

    /*
     * 顺序固定为：旧环境广播 -> move_object 真正换环境 -> 新环境广播 -> 完成回调。
     * on_leave/on_arrive 都依赖这一前后状态，子类不要改成先到达后离开。
     */
    if (objectp(old_env) && !silent) {
        leave_dir = find_exit_direction(old_env, dest);
        arrive_dir = find_exit_direction(dest, old_env);
        if ((!stringp(arrive_dir) || arrive_dir == "") && stringp(leave_dir) && leave_dir != "")
            arrive_dir = opposite_direction(leave_dir);

        me->set_temp("move/context/old_env", old_env);
        me->set_temp("move/context/dest", dest);
        me->set_temp("move/context/leave_dir", leave_dir);
        me->set_temp("move/context/arrive_dir", arrive_dir);
        on_leave(old_env);
    }

    move_object(dest);
    if (environment(me) != dest)
    {
        me->delete_temp("move/context");
        if (should_log_move_debug(me))
            log_file("system/move_debug", sprintf("move_object failed obj=%O old=%O dest=%O new_env=%O\n", me, old_env, dest, environment(me)));
        return 0;
    }

    mark_room_view_transition(old_env, dest);
    record_loadtest_move_trace(me, old_env, dest);

    if (!silent) {
        on_arrive(dest);
    }

    on_move_complete(old_env, dest);
    me->delete_temp("move/context");

    return 1;
}

/**
 * @brief 移动到指定房间（兼容旧接口）
 * @param dest 目标房间
 * @return 1成功，0失败
 */
int move(mixed dest)
{
    object dest_ob;

    if (objectp(dest)) {
        dest_ob = dest;
    } else if (stringp(dest)) {
        dest_ob = find_object(dest);
        if (!objectp(dest_ob))
            dest_ob = load_object(dest);
    }

    return move_to(dest_ob, 0);
}

/**
 * @brief 检查是否可以移动到目标
 * @param dest 目标环境
 * @return 1可以，0不可以
 */
int can_move(object dest)
{
    mixed no_enter;
    mixed no_leave;
    object old_env;

    if (!objectp(dest))
        return 0;

    old_env = environment(this_object());

    if (objectp(old_env)) {
        no_leave = old_env->query("no_leave");
        if (intp(no_leave) && no_leave)
            return 0;
    }

    no_enter = dest->query("no_enter");
    if (intp(no_enter) && no_enter)
        return 0;

    return on_can_move(dest);
}

/**
 * @brief 检查是否可以在环境中移动
 * @return 1可以，0不可以
 */
int can_move_in_environment()
{
    object env;
    mixed no_move;

    env = environment(this_object());
    if (!objectp(env))
        return 0;

    no_move = env->query("no_move");
    if (intp(no_move) && no_move)
        return 0;

    return 1;
}

/**
 * @brief 移动前检查回调
 * @param dest 目标环境
 * @return 1允许移动，0阻止移动
 * @note 子类可以覆盖此方法
 */
int on_can_move(object dest)
{
    return 1;
}

private string room_short_name(object room)
{
    string name;

    if (!objectp(room))
        return "未知地点";

    name = room->query("short");
    if (!stringp(name) || name == "")
        name = get_object_name(room);
    if (!stringp(name) || name == "")
        name = base_name(room);

    return name;
}

private string short_direction_name(string dir)
{
    mapping names;

    names = ([
        "north" : "北", "south" : "南",
        "east" : "东", "west" : "西",
        "northeast" : "东北", "northwest" : "西北",
        "southeast" : "东南", "southwest" : "西南",
        "up" : "上", "down" : "下",
        "enter" : "里", "out" : "外"
    ]);

    return names[dir] || dir;
}

private string opposite_direction(string dir)
{
    mapping opposites;

    opposites = ([
        "north" : "south", "south" : "north",
        "east" : "west", "west" : "east",
        "northeast" : "southwest", "southwest" : "northeast",
        "northwest" : "southeast", "southeast" : "northwest",
        "up" : "down", "down" : "up",
        "enter" : "out", "out" : "enter"
    ]);

    return opposites[dir] || "";
}

private string find_exit_direction(object from_room, object to_room)
{
    mapping exits;
    string dir;
    string target_path;

    if (!objectp(from_room) || !objectp(to_room))
        return "";

    exits = from_room->query("exits");
    if (!mapp(exits))
        return "";

    target_path = base_name(to_room);
    foreach (dir in keys(exits)) {
        if (stringp(exits[dir]) && exits[dir] == target_path)
            return dir;
    }

    return "";
}

private mapping movement_room_message_options(object room)
{
    return ([
        "channel": "main",
        "intent": "append",
        "priority": "low",
        "reliability": "important",
        "display_mode": "instant",
        "payload": ([ "kind": "room_movement" ]),
    ]);
}

/**
 * @brief 离开环境回调
 * @param old_env 旧环境
 * @return 无返回值
 * @note 子类可以覆盖此方法
 */
void on_leave(object old_env)
{
    string my_name;
    string dir;
    string dest_name;
    object dest;

    if (!objectp(old_env))
        return;
    if (!living(this_object())
        && (!function_exists("is_character", this_object())
            || !call_other(this_object(), "is_character")))
        return;

    my_name = this_object()->query("name") || "某人";
    dest = this_object()->query_temp("move/context/dest");
    dir = this_object()->query_temp("move/context/leave_dir");
    if (!stringp(dir) || dir == "")
        return;

    dest_name = room_short_name(dest);

    // 向旧环境中的其他对象发送离开消息
    msg_to_room_event(old_env, sprintf("%s向%s（%s）走了。\n", my_name, short_direction_name(dir), dest_name),
        ({ this_object() }), movement_room_message_options(old_env));
}

/**
 * @brief 到达环境回调
 * @param new_env 新环境
 * @return 无返回值
 * @note 子类可以覆盖此方法
 */
void on_arrive(object new_env)
{
    string my_name;
    string dir;
    string old_name;
    object old_env;

    if (!objectp(new_env))
        return;
    if (!living(this_object())
        && (!function_exists("is_character", this_object())
            || !call_other(this_object(), "is_character")))
        return;
    if (new_env->query_temp("room/suppress_arrival_messages"))
        return;

    my_name = this_object()->query("name") || "某人";
    old_env = this_object()->query_temp("move/context/old_env");
    dir = this_object()->query_temp("move/context/arrive_dir");

    /* 玩家本人到达消息：你来到了xxx */
    if (userp(this_object()))
        msg_send(this_object(), sprintf("你来到了%s。\n",
            room_short_name(new_env) || new_env->query("title") || "未知地点"));

    if (!stringp(dir) || dir == "")
        return;

    old_name = room_short_name(old_env);

    // 向新环境中的其他对象发送到达消息
    msg_to_room_event(new_env, sprintf("%s从%s（%s）来到这里。\n", my_name, short_direction_name(dir), old_name),
        ({ this_object() }), movement_room_message_options(new_env));
}

/**
 * @brief 移动完成回调
 * @param old_env 旧环境
 * @param new_env 新环境
 * @return 无返回值
 * @note 子类可以覆盖此方法
 */
void on_move_complete(object old_env, object new_env)
{
    // 这里留给跨层补同步，调用时机保证 environment() 已经是 new_env。
    // 子类如需 look、房间协议补发或状态修正，应优先挂在这一层。
}

// 位置查询
/**
 * @brief 查询当前位置描述
 * @return 位置描述字符串
 */
string query_location_desc()
{
    object env;
    string env_name;

    env = environment(this_object());

    if (!objectp(env))
        return "虚空之中";

    env_name = env->query("short");
    if (!stringp(env_name) || env_name == "")
        env_name = "未知地点";

    return env_name;
}

/**
 * @brief 查询当前坐标（如果环境支持）
 * @return 坐标数组 ({ x, y, z })，不支持返回0
 */
int *query_coordinates()
{
    object env;
    mixed coords;

    env = environment(this_object());
    if (!objectp(env))
        return 0;

    coords = env->query("coordinates");
    if (arrayp(coords) && sizeof(coords) >= 2)
        return coords;

    return 0;
}

// 容器操作
/**
 * @brief 检查对象是否包含某物
 * @param item 要检查的物品
 * @return 1包含，0不包含
 */
int contain_object(object item)
{
    object *inv;

    if (!objectp(item))
        return 0;

    inv = all_inventory(this_object());
    return (member_array(item, inv) != -1);
}

/**
 * @brief 将物品放入此对象
 * @param item 物品对象
 * @return 1成功，0失败
 */
int put_in(object item)
{
    if (!objectp(item))
        return 0;

    // 调用物品的move函数
    return item->move(this_object());
}

/**
 * @brief 从对象中取出物品
 * @param item 物品对象
 * @param dest 目标环境
 * @return 1成功，0失败
 */
int take_out(object item, object dest)
{
    if (!objectp(item))
        return 0;

    if (!objectp(dest))
        dest = environment(this_object());

    if (!contain_object(item))
        return 0;

    return item->move(dest);
}

// 创建和清理
/**
 * @brief 创建时初始化
 * @return 无返回值
 */
void create()
{
    // 移动框架无需特殊初始化
    // 位置由 move_object 决定
}

/**
 * @brief 设置初始位置
 * @param dest 初始环境
 * @return 1成功，0失败
 */
int setup_initial_location(object dest)
{
    if (!objectp(dest))
        return 0;

    return move_to(dest, 1); // 静默移动
}
