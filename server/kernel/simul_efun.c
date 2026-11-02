/**
 * @file    simul_efun.c
 * @brief   模拟外部函数
 * @author  XiaKeXing Development Team
 * @date    2026-03-20
 * @version 1.0.1
 *
 * @par Description:
 * 提供项目必需的模拟外部函数：
 * - 对象相关函数 (get_object_id, real_id)
 * - 日志函数 (log_file)
 * - 性别系统 (gender_self, gender_pronoun)
 * - 消息系统 (tell_object, tell_room, say, shout, message_vision)
 */

#pragma strict_types

#include <system/globals.h>
#include <system/room_meta_helpers.h>
#include <system/services.h>
#include <system/ansi.h>
#include <system/xk_protocol.h>
#include <framework/message_facade.h>

inherit "/lib/std/json";

#define XK_PROTOCOL_CORE "/kernel/framework/xk_protocol_core"
#define DAEMON_FRAMEWORK "/lib/framework/daemon"
#define SIMUL_EFUN_LOGIN_XK_HELPER "/kernel/simul_efun_login_xk"
#define SIMUL_EFUN_BOOTSTRAP_HELPER "/kernel/simul_efun_bootstrap"
#define SIMUL_EFUN_COMMAND_COMPAT_HELPER "/kernel/simul_efun_command_compat"
#define SIMUL_EFUN_MESSAGE_HELPER "/kernel/simul_efun_message"
#define SIMUL_EFUN_OBJECT_IDENTITY_HELPER "/kernel/simul_efun_object_identity"
#define SIMUL_EFUN_PLAYER_XK_HELPER "/kernel/simul_efun_player_xk"
#define SIMUL_EFUN_VISION_HELPER "/kernel/simul_efun_vision"

varargs string get_object_id(object ob);
void xk_send_chat_packet(object me, string type, string msg);

/**
 * @brief 获取对象真实ID（防伪装）
 * @param ob 对象
 * @return 真实ID
 */
string real_id(object ob)
{
    return call_other(SIMUL_EFUN_OBJECT_IDENTITY_HELPER, "real_id", ob);
}

varargs string get_object_name(object ob)
{
    return call_other(SIMUL_EFUN_OBJECT_IDENTITY_HELPER, "get_object_name", ob);
}

/**
 * @brief 安全获取对象的ID字符串
 * @param ob 目标对象（可选，默认为this_object()）
 * @return ID字符串，如果无法获取则返回 ""
 *
 * 说明:
 *   - 优先调用对象的 query_id() 方法
 *   - 如果 query_id() 不存在，则从 dbase 查询 "id" 属性
 *   - 处理字符串和数组类型的 id 值
 *   - 避免 Bad type argument to + 错误
 */
varargs string get_object_id(object ob)
{
    return call_other(SIMUL_EFUN_OBJECT_IDENTITY_HELPER, "get_object_id", ob);
}

/**
 * @brief 检查对象是否为玩家
 * @param ob 对象
 * @return 1=是玩家, 0=否
 */
int playerp(object ob)
{
    return call_other(SIMUL_EFUN_OBJECT_IDENTITY_HELPER, "playerp", ob);
}

/**
 * @brief 检查对象是否为用户
 * @param ob 对象
 * @return 1=是用户, 0=否
 */
int userp(object ob)
{
    return call_other(SIMUL_EFUN_OBJECT_IDENTITY_HELPER, "userp", ob);
}

/**
 * @brief 强制执行命令
 * @param cmd 命令字符串
 */
protected void force_me(string cmd)
{
    call_other(SIMUL_EFUN_COMMAND_COMPAT_HELPER, "force_me", cmd);
}

/**
 * @brief 日志文件记录函数
 * @param file LOG_DIR 内的相对路径，可包含子目录
 * @param message 日志消息
 * @return 1=已追加；0=非法路径或写入失败
 */
int log_file(string file, string message)
{
    return call_other(SIMUL_EFUN_LOG_HELPER, "log_file", file, message);
}

/**
 * @brief 获取第一人称代词（自己视角）
 * @param sex 性别
 * @return 代词
 */
string gender_self(string sex)
{
    return call_other(SIMUL_EFUN_VISION_HELPER, "gender_self", sex);
}

/**
 * @brief 获取第三人称代词（他人视角）
 * @param sex 性别
 * @return 代词
 */
string gender_pronoun(string sex)
{
    return call_other(SIMUL_EFUN_VISION_HELPER, "gender_pronoun", sex);
}

/**
 * @brief 向指定对象发送消息
 * @param ob 目标对象
 * @param str 消息内容
 */
varargs void msg_send(object ob, string str, mapping response_context)
{
    call_other(SIMUL_EFUN_MESSAGE_HELPER, "msg_send", ob, str,
        response_context);
}

void msg_send_raw(object ob, string str)
{
    call_other(SIMUL_EFUN_MESSAGE_HELPER, "msg_send_raw", ob, str);
}

/* XK protocol core may use this only after it has committed the frame to the
 * gateway session FIFO.  Other callers retain the normal raw-text boundary. */
/**
 * @brief 向房间内所有对象发送消息
 * @param room 房间对象或路径
 * @param str 消息内容
 * @param exclude 排除的对象列表
 */
varargs void msg_to_room(mixed room, string str, mixed exclude)
{
    call_other(SIMUL_EFUN_MESSAGE_HELPER, "msg_to_room", room, str, exclude);
}

varargs void msg_to_room_raw(mixed room, string str, mixed exclude)
{
    call_other(SIMUL_EFUN_MESSAGE_HELPER, "msg_to_room_raw", room, str, exclude);
}

varargs void msg_to_room_event(mixed room, string str, mixed exclude, mapping options)
{
    call_other(SIMUL_EFUN_MESSAGE_HELPER, "msg_to_room_event", room, str, exclude, options);
}

varargs void msg_room(mixed room, string str, mixed exclude)
{
    call_other(SIMUL_EFUN_MESSAGE_HELPER, "msg_room", room, str, exclude);
}

/**
 * @brief 在当前环境中说话
 * @param str 消息内容
 * @param exclude 排除的对象
 */
varargs void say(string str, mixed exclude)
{
    call_other(SIMUL_EFUN_MESSAGE_HELPER, "say", str, exclude);
}

/**
 * @brief 发送同房说话的文本/XK 双通道消息
 * @param speaker 说话者
 * @param msg 原始发言内容
 *
 * @note `say` 是房间广播里最容易被协议和文本链各自复制实现的入口。
 *       这里统一收口到 simul_efun：
 *       - 发送者收到“你说道”文本 + 自身 XKCHAT
 *       - 同房其他角色收到 msg_to_room 文本 + XKCHAT
 *       这样命令层只负责参数解析，不再自己扇出房间消息。
 */
void emit_room_say_ui(object speaker, string msg)
{
    call_other(SIMUL_EFUN_MESSAGE_HELPER, "emit_room_say_ui", speaker, msg);
}

/**
 * @brief 全服广播
 * @param str 消息内容
 */
void msg_broadcast(string str)
{
    call_other(SIMUL_EFUN_MESSAGE_HELPER, "msg_broadcast", str);
}

/**
 * @brief 视觉消息系统（支持$N$n等占位符）
 * @param msg 消息模板
 * @param me 主对象
 * @param you 目标对象（可选）
 */
varargs void message_vision(string msg, object me, object you)
{
    call_other(SIMUL_EFUN_VISION_HELPER, "message_vision", msg, me, you);
}

/**
 * @brief simul_efun初始化
 */
void create()
{
    seteuid(getuid(this_object()));
    call_other(SIMUL_EFUN_BOOTSTRAP_HELPER, "preload_helpers", ({
        SIMUL_EFUN_COMMAND_COMPAT_HELPER,
        SIMUL_EFUN_LOGIN_XK_HELPER,
        SIMUL_EFUN_LOG_HELPER,
        SIMUL_EFUN_MESSAGE_HELPER,
        SIMUL_EFUN_OBJECT_IDENTITY_HELPER,
        SIMUL_EFUN_PLAYER_XK_HELPER,
        SIMUL_EFUN_ROOM_ENTITIES_XK_HELPER,
        SIMUL_EFUN_ROOM_TITLE_XK_HELPER,
        SIMUL_EFUN_ROOM_EXITS_XK_HELPER,
        SIMUL_EFUN_ROOM_MARKER_XK_HELPER,
        SIMUL_EFUN_ROOM_MAPGRID_XK_HELPER,
        SIMUL_EFUN_ROOM_MAPREAD_XK_HELPER,
        SIMUL_EFUN_ROOM_SYNC_XK_HELPER,
        SIMUL_EFUN_VISION_HELPER,
    }));
}

/**
 * @brief 发送快捷命令按钮（XK协议）
 * @param me 玩家对象
 * @param cmds 快捷命令 payload；当前兼容 mapping 数组或带标题的 legacy 字符串格式
 * @param priority 优先级
 */
varargs void xk_send_quick_cmds(object me, mixed cmds, int priority,
    mapping response_context)
{
    call_other(SIMUL_EFUN_PLAYER_XK_HELPER, "xk_send_quick_cmds", me, cmds,
        priority, response_context);
}

varargs void xk_send_secondary_cmds(object me, mixed cmds, string title,
    int priority, mapping response_context)
{
    call_other(SIMUL_EFUN_PLAYER_XK_HELPER, "xk_send_secondary_cmds", me,
        cmds, title, priority, response_context);
}

varargs void xk_send_dynamic_buttons(object me, mixed cmds, string title,
    int priority, mapping response_context)
{
    call_other(SIMUL_EFUN_PLAYER_XK_HELPER, "xk_send_dynamic_buttons", me,
        cmds, title, priority, response_context);
}

varargs void xk_send_skill_list(object me, mapping response_context)
{
    call_other(SIMUL_EFUN_PLAYER_XK_HELPER, "xk_send_skill_list", me,
        response_context);
}

varargs void xk_send_skill_info(object me, string skill_id,
    mapping response_context)
{
    call_other(SIMUL_EFUN_PLAYER_XK_HELPER, "xk_send_skill_info", me, skill_id,
        response_context);
}

varargs int xk_send_status(object me, mapping response_context)
{
    return call_other(SIMUL_EFUN_PLAYER_XK_HELPER, "xk_send_status", me,
        response_context);
}

varargs void xk_send_status_light(object me, mapping response_context)
{
    call_other(SIMUL_EFUN_PLAYER_XK_HELPER, "xk_send_status_light", me,
        response_context);
}

varargs void xk_send_objects(object me, object *items,
    mapping response_context)
{
    call_other(SIMUL_EFUN_ROOM_ENTITIES_XK_HELPER, "xk_send_objects", me,
        items, response_context);
}

varargs int xk_send_inventory(object me, object *items,
    mapping response_context)
{
    return call_other(SIMUL_EFUN_ROOM_ENTITIES_XK_HELPER, "xk_send_inventory", me,
        items, response_context);
}

void invalidate_xk_inventory_cache(object me)
{
    call_other(SIMUL_EFUN_ROOM_ENTITIES_XK_HELPER, "invalidate_inventory_payload_cache", me);
}

varargs void xk_send_npcs(object me, object *npcs,
    mapping response_context)
{
    call_other(SIMUL_EFUN_ROOM_ENTITIES_XK_HELPER, "xk_send_npcs", me, npcs,
        response_context);
}

void xk_send_title(object me, string title, string zone)
{
    call_other(SIMUL_EFUN_ROOM_TITLE_XK_HELPER, "xk_send_title", me, title, zone);
}

string resolve_room_display_name(string room_path)
{
    return call_other(SIMUL_EFUN_ROOM_TITLE_XK_HELPER, "resolve_room_display_name", room_path);
}

void xk_send_exits(object me, mapping exits)
{
    call_other(SIMUL_EFUN_ROOM_EXITS_XK_HELPER, "xk_send_exits", me, exits);
}

varargs int xk_send_map_grid(object me, string title, string zone,
    mapping exits, int force_snapshot, mapping response_context)
{
    return call_other(SIMUL_EFUN_ROOM_MAPGRID_XK_HELPER, "xk_send_map_grid",
        me, title, zone, exits, force_snapshot, response_context);
}

varargs int xk_emit_room_snapshot(object me, object room,
    mapping response_context)
{
    return call_other(SIMUL_EFUN_ROOM_SYNC_XK_HELPER,
        "xk_emit_room_snapshot", me, room, response_context);
}

varargs int xk_emit_room_navigation_snapshot(object me, object room,
    mapping response_context)
{
    return call_other(SIMUL_EFUN_ROOM_SYNC_XK_HELPER,
        "xk_emit_room_navigation_snapshot", me, room, response_context);
}

void xk_send_register(object me, string stage, string message)
{
    call_other(SIMUL_EFUN_LOGIN_XK_HELPER, "xk_send_register", me, stage, message);
}

void xk_send_login_prompt(object me)
{
    call_other(SIMUL_EFUN_LOGIN_XK_HELPER, "xk_send_login_prompt", me);
}

void xk_send_chat_packet(object me, string type, string msg)
{
    call_other(SIMUL_EFUN_MESSAGE_HELPER, "xk_send_chat_packet", me, type, msg);
}

int xk_send_chat_packet_ex(object me, string type, string msg, mapping extra)
{
    return call_other(SIMUL_EFUN_MESSAGE_HELPER, "xk_send_chat_packet_ex", me,
        type, msg, mapp(extra) ? extra : ([]));
}

varargs void xk_send_dialog(object me, string title, string content,
    mapping extra, mapping response_context)
{
    mapping payload_extra;

    payload_extra = mapp(extra) ? copy(extra) : ([]);
    if (mapp(response_context))
        payload_extra["response_context"] = copy(response_context);
    call_other(XK_PROTOCOL_CORE, "xk_send_dialog", me, title, content,
        payload_extra);
}

void xk_send_text_packet(object me, string text, mapping options)
{
    call_other(XK_PROTOCOL_CORE, "xk_send_text_packet", me, text,
        mapp(options) ? options : ([]));
}

varargs void xk_send_system(object me, string text, string level,
    mapping response_context)
{
    call_other(DAEMON_FRAMEWORK, "send_xk_system", me, text, level,
        response_context);
}

varargs int xk_send_batch(object me, mixed messages,
    mapping response_context)
{
    return call_other(XK_PROTOCOL_CORE, "xk_send_batch", me,
        messages, response_context);
}

int emit_world_shout_ui(object me, string msg)
{
    return call_other(SIMUL_EFUN_MESSAGE_HELPER, "emit_world_shout_ui", me, msg);
}
