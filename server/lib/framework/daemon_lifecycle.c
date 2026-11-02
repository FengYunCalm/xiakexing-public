/**
 * @file    daemon_lifecycle.c
 * @brief   daemon 基类生命周期与在线玩家查询入口（由 daemon.c 文本包含）
 *
 * daemon.c 受 architecture ratchet（`daemon_framework_lines` 只减不增）约束，
 * 生命周期基础方法单独放入 fragment；编译与运行仍是同一翻译单元。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

private mapping resolve_xk_response_context(object me, mapping response_context)
{
    /* 后台/延迟 facade 没有显式冻结上下文时必须发送无关联事实；
     * 禁止在发送时读取玩家“最新请求”补标签。 */
    if (undefinedp(response_context))
        return 0;
    return mapp(response_context) ? copy(response_context) : 0;
}

/**
 * @brief Resolve a persistent account ID against the canonical online-player index.
 */
object find_player_online(string player_name)
{
    return call_other("/lib/std/utils/util_player", "find_player_online", player_name);
}

/**
 * @brief 守护进程创建时初始化
 */
void create()
{
    call_other(this_object(), "setup");
}

/**
 * @brief 守护进程设置（子类可以覆盖）
 */
void setup()
{
}

/**
 * @brief 获取守护进程名称
 * @return 守护进程名称
 */
string query_daemon_name()
{
    string fname;
    string *parts;

    fname = file_name(this_object());
    parts = explode(fname, "/");
    return parts[sizeof(parts) - 1];
}

/**
 * @brief 日志记录
 * @param msg 日志消息
 */
void log_daemon(string msg)
{
    string name;

    name = query_daemon_name();
    log_file(sprintf("daemon/%s", name), sprintf("[%s] %s\n", ctime(time())[4..19], msg));
}

/**
 * @brief 错误处理
 * @param error 错误信息
 */
void handle_error(string error)
{
    log_daemon(sprintf("ERROR: %s", error));
    debug_message(sprintf("[%s] Error: %s", query_daemon_name(), error));
}
