/**
 * @file    master.c
 * @brief   FluffOS 主控对象
 * @author  XiaKeXing Development Team
 * @date    2026-03-20
 * @version 1.0.1
 *
 * @par Description:
 * FluffOS 驱动程序的主控对象，负责核心系统功能：
 * - 连接管理 (connect)
 * - 权限验证 (valid_*)
 * - 错误处理与报告 (error_handler)
 * - UID 管理 (get_root_uid, get_bb_uid)
 *
 * 普通玩家会收到友好的错误提示，巫师会收到完整堆栈，同时错误写入日志。
 */

#pragma strict_types

#include <system/ansi.h>
#include <system/globals.h>
#include <system/services.h>
#include <framework/message_facade.h>
#include <game_constants.h>

// master 不是业务基类对象，错误提示不能假设 msg_send 已由 inherit 链提供。
void msg_send(object ob, string msg)
{
    call_other("/kernel/simul_efun", "msg_send", ob, msg);
}

mapping query_gateway_status_extension()
{
    mapping status;
    mixed err;

    err = catch(status = CLIENT_SYNC_D->query_status());
    if (err || !mapp(status))
        return ([]);

    return ([
        "client_sync_pending_entries": status["pending_entries"] || 0,
        "client_sync_chat_pending": status["client_sync_chat_pending"] || 0,
        "client_sync_chat_queue_depth":
            status["client_sync_chat_queue_depth"] || 0,
        "client_sync_chat_active_wave_remaining":
            status["client_sync_chat_active_wave_remaining"] || 0,
        "client_sync_chat_consumer_armed":
            status["client_sync_chat_consumer_armed"] || 0,
        "client_sync_chat_consumer_callout_valid":
            status["client_sync_chat_consumer_callout_valid"] || 0,
        "client_sync_chat_consumer_orphaned":
            status["client_sync_chat_consumer_orphaned"] || 0,
        "client_sync_chat_fanout_queue_depth":
            status["client_sync_chat_fanout_queue_depth"] || 0,
        "client_sync_chat_fanout_pending_targets":
            status["client_sync_chat_fanout_pending_targets"] || 0,
        "client_sync_chat_fanout_active_wave_jobs_remaining":
            status["client_sync_chat_fanout_active_wave_jobs_remaining"] || 0,
        "client_sync_chat_fanout_wave_recipient_count":
            status["client_sync_chat_fanout_wave_recipient_count"] || 0,
        "client_sync_score_refresh_pending":
            status["client_sync_score_refresh_pending"] || 0,
        "client_sync_score_refresh_queue_depth":
            status["client_sync_score_refresh_queue_depth"] || 0,
    ]);
}

void prepare_shutdown(int exit_code)
{
    mixed err;

    err = catch(load_object(SHUTDOWN_D));
    if (err) {
        log_file("system/master", sprintf(
            "[%s] prepare_shutdown: failed to load %s: %O\n",
            ctime(time()), SHUTDOWN_D, err));
        return;
    }

    err = catch(SHUTDOWN_D->prepare_for_driver_shutdown(exit_code));
    if (err)
        log_file("system/master", sprintf(
            "[%s] prepare_shutdown: callback failed exit_code=%d: %O\n",
            ctime(time()), exit_code, err));
}

void crash(string crash_message, object command_giver, object current_object)
{
    log_file("system/master", sprintf(
        "[%s] fatal crash before driver abort: %s command_giver=%s current_object=%s\n",
        ctime(time()), crash_message,
        objectp(command_giver) ? file_name(command_giver) : "0",
        objectp(current_object) ? file_name(current_object) : "0"));
    prepare_shutdown(-1);
}

/**
 * @brief 格式化错误信息
 * @param error 错误映射
 * @param caught 是否被捕获
 * @return 格式化后的错误字符串
 *
 * 生成详细的错误报告，包含：
 * - 错误消息
 * - 程序文件和行号
 * - 对象信息
 * - 调用堆栈
 */
string standard_trace(mapping error, int caught)
{
    int i, s;
    string res;
    object me;

    res = (caught) ? "错误讯息被拦截：" : "";
    res = sprintf("%s\n执行时段错误：%s\n程式：%s 第 %i 行\n物件：%s\n",
                  res, error["error"],
                  error["program"], error["line"],
                  error["object"] ? file_name(error["object"]) : "0");

    if (!error["object"] && (me = this_player())) {
        res += sprintf("当前玩家：%s(%s) 所在环境：%s\n",
                       me->name(1) || me->query("name") || "无名",
                       get_object_id(me) || "unknown",
                       environment(me) ? file_name(environment(me)) : "虚空");
    }

    // 添加调用堆栈
    if (arrayp(error["trace"])) {
        s = sizeof(error["trace"]);
        for (i = 0; i < s && i < 10; i++)
        {
            res = sprintf("%s呼叫来自：%s 的 %s() 第 %i 行\n",
                          res,
                          error["trace"][i]["program"] || "未知",
                          error["trace"][i]["function"] || "未知",
                          error["trace"][i]["line"] || 0);
        }
    }
    return res;
}

string write_error_logs(string prefix, string trace_text)
{
    string detail_log;
    mixed err;

    // 错误报告写入统一由内核日志 helper 承担：明细有界保留、索引轮转、失败不递归。
    err = catch(detail_log = call_other(SIMUL_EFUN_LOG_HELPER, "write_error_report",
                                        prefix, trace_text + "\n"));
    if (err || !stringp(detail_log) || detail_log == "") {
        debug_message(sprintf("[master] error report write failed prefix=%s: %O\n", prefix, err));
        return "";
    }
    return detail_log;
}

private string normalize_file_path(string path)
{
    if (!stringp(path) || path == "")
        return "";

    if (path[0] != '/')
        return "/" + path;

    return path;
}

private string query_request_uid(mixed user)
{
    if (objectp(user))
        return geteuid(user) || getuid(user) || "";

    if (stringp(user))
        return user;

    return "";
}

private string query_request_file(mixed user)
{
    if (objectp(user))
        return file_name(user) || "";

    return "";
}

private object resolve_recovery_room()
{
    string room_path;
    mixed err;
    object room;

    room_path = "";
    if (find_object(CONFIG_D) || catch(load_object(CONFIG_D)) == 0)
        room_path = CONFIG_D->query_start_room();
    if (!stringp(room_path) || room_path == "") {
        log_file("system/master", sprintf("[%s] resolve_recovery_room: no configured start room\n",
            ctime(time())));
        return 0;
    }

    err = catch(room = load_object(room_path));
    if (err || !objectp(room)) {
        log_file("system/master", sprintf("[%s] resolve_recovery_room: failed to load %s: %O\n",
            ctime(time()), room_path, err));
        return 0;
    }

    return room;
}

private int is_data_path(string path)
{
    path = normalize_file_path(path);
    return stringp(path) && strsrch(path, DATA_DIR) == 0;
}

private int is_config_path(string path)
{
    path = normalize_file_path(path);
    return stringp(path) && strsrch(path, "/config/") == 0;
}

private int is_internal_source_path(string path)
{
    path = normalize_file_path(path);
    if (!stringp(path) || path == "")
        return 0;

    return strsrch(path, "/kernel/") == 0
        || strsrch(path, "/lib/") == 0
        || strsrch(path, "/cmds/") == 0
        || strsrch(path, "/daemons/") == 0
        || strsrch(path, "/world/") == 0
        || strsrch(path, "/config/") == 0;
}

private int is_runtime_read_path(string path)
{
    return is_data_path(path) || is_config_path(path);
}

private int is_runtime_write_path(string path)
{
    return is_data_path(path);
}

private int is_internal_request(mixed user)
{
    string request_uid;
    string request_file;

    request_uid = query_request_uid(user);
    if (request_uid == ROOT_UID || request_uid == BACKBONE_UID)
        return 1;

    request_file = query_request_file(user);
    if (!stringp(request_file) || request_file == "")
        return 0;

    return is_internal_source_path(request_file);
}

private int is_world_root_path(string path)
{
    object config_d;
    string *roots;
    int i;

    path = normalize_file_path(path);
    if (!stringp(path) || path == "")
        return 0;

    /* No historical world root is a valid fallback while the canonical map is
     * awaiting design; CONFIG_D is the sole source once a map is published. */
    roots = ({ });
    config_d = find_object(CONFIG_D);
    if (objectp(config_d)) {
        mixed configured_roots;

        configured_roots = config_d->query_world_roots();
        if (arrayp(configured_roots) && sizeof(configured_roots) > 0)
            roots = configured_roots;
    }

    for (i = 0; i < sizeof(roots); i++) {
        if (stringp(roots[i]) && strsrch(path, roots[i]) == 0)
            return 1;
    }

    return 0;
}

/**
 * @brief 运行时错误处理器
 * @param error 错误映射
 * @param caught 是否被捕获
 * @return 格式化后的错误字符串
 *
 * FluffOS 在发生运行时错误时调用此函数
 */
private void emit_error_output(object player, string text)
{
    mixed err;

    if (!objectp(player) || !stringp(text) || text == "")
        return;
    err = catch(call_other("/kernel/simul_efun", "msg_send_raw", player, text));
    if (err)
        debug_message(sprintf("[master] error output dispatch failed: %O", err));
}

string error_handler(mapping error, int caught)
{
    object player;
    string trace_text;
    string detail_log;

    player = this_player(1) || this_player();
    trace_text = standard_trace(error, caught);
    if (player && caught && player->query_temp("runtime/suppress_caught_error"))
        return "caught runtime suppressed\n";

    detail_log = write_error_logs("runtime", trace_text);

    if (player) {
        // 存储错误信息供 bug 命令使用
        player->set_temp("last_error", error);
        player->set_temp("last_error_time", time());

        if (wizardp(player)) {
            emit_error_output(player,
                HIR "\n━━━━━━ 系统错误 ━━━━━━\n" NOR);
            emit_error_output(player, trace_text);
            if (detail_log != "")
                emit_error_output(player,
                    sprintf(HIC "日志文件：%s\n" NOR, detail_log));
            else
                emit_error_output(player,
                    HIR "错误日志写入失败，仅本次显示。\n" NOR);
            emit_error_output(player,
                HIR "━━━━━━━━━━━━━━━━━━━━\n" NOR);
        } else {
            emit_error_output(player,
                HIR "系统处理命令时遇到异常，已记录日志。\n" NOR);
        }
    }

    return trace_text;
}

/**
 * @brief 编译错误日志
 * @param file 文件路径
 * @param error 错误信息
 *
 * 当代码编译失败时调用
 */
void log_error(string file, string error)
{
    string time_str;
    string trace_text;

    time_str = ctime(time())[4..15];
    trace_text = sprintf("[%s] %s\n%s\n", time_str, file, error);

    write_error_logs("compile", trace_text);
}

/**
 * @brief 处理新连接
 * @return 登录对象
 */
object connect()
{
    object login_ob;
    object receiver;
    mixed err;

    err = catch(login_ob = new("/kernel/login"));

    if (err)
    {
        receiver = this_player();
        if (objectp(receiver))
            msg_send(receiver, "登录系统错误，请联系管理员。\n");
        else
            /*
             * `connect()` 处在 driver 入口期，失败时不一定已经有一个能安全
             * 走 `msg_send()` 的接收对象。这里保留 `write()` 作为 driver-level
             * 最后兜底输出，而不是误把它当成普通业务层消息接口。
             */
            write("登录系统错误，请联系管理员。\n");
        log_error("master", sprintf("创建登录对象失败: %s", err));
        return 0;
    }

    return login_ob;
}

/**
 * @brief 获取Root UID
 * @return "Root"
 */
string get_root_uid() { return "Root"; }

/**
 * @brief 获取Backbone UID
 * @return "Backbone"
 */
string get_bb_uid() { return "Backbone"; }

/**
 * @brief 获取文件创建者
 * @param str 文件路径
 * @return 创建者UID
 */
string creator_file(string str) { return "Root"; }

/**
 * @brief 获取文件作者
 * @param file 文件路径
 * @return 作者UID
 */
string author_file(string file)
{
    if (!stringp(file)) return 0;
    if (is_world_root_path(file)) return "world";
    if (file[0..7] == "/kernel/" || file[0..7] == "/system/")
        return "Root";
    return "Root";
}

/**
 * @brief 获取文件域
 * @param file 文件路径
 * @return 域名
 */
string domain_file(string file)
{
    if (!stringp(file)) return 0;
    if (is_world_root_path(file)) return "world";
    if (file[0..7] == "/kernel/" || file[0..7] == "/system/" ||
        file[0..6] == "/config")
        return "system";
    return "common";
}

/**
 * @brief 验证euid切换权限
 * @param obj 对象
 * @param euid 目标euid
 * @return 1=允许, 0=拒绝
 */
int valid_seteuid(object obj, string euid)
{
    string obj_euid;
    string obj_file;

    if (!objectp(obj) || !stringp(euid)) return 0;

    obj_euid = geteuid(obj);
    obj_file = file_name(obj);

    // Root可以切换到任何euid
    if (obj_euid == ROOT_UID) return 1;

    // 对象可以切换到自己的uid
    if (euid == obj_euid) return 1;

    // 系统目录下的对象有更高权限
    if (strsrch(obj_file, "/system/") == 0 ||
        strsrch(obj_file, "/kernel/") == 0) {
        return 1;
    }

    // 其他情况拒绝
    return 0;
}

/**
 * @brief 验证efun覆盖权限
 * @param file 文件路径
 * @param efun_name efun名称
 * @param path 路径
 * @return 1=允许, 0=拒绝
 */
int valid_override(string file, string efun_name, string path)
{
    if (!stringp(file) || !stringp(efun_name))
        return 0;

    if (file == "/kernel/simul_efun")
        return 1;

    return 0;
}

/**
 * @brief 验证对象是否有效
 * @param ob 对象
 * @return 1=有效, 0=无效
 */
int valid_object(object ob)
{
    string file;

    if (!objectp(ob)) return 0;

    file = file_name(ob);

    // 系统对象始终有效
    if (strsrch(file, "/system/") == 0 ||
        strsrch(file, "/kernel/") == 0) {
        return 1;
    }

    // 玩家对象有效
    if (userp(ob)) return 1;

    // 其他对象需要检查
    return 1;
}

/**
 * @brief 验证对象绑定权限
 * @param binder 绑定者
 * @param old_ob 旧对象
 * @param fun 函数名
 * @return 1=允许, 0=拒绝
 */
int valid_bind(object binder, object old_ob, string fun)
{
    string binder_file;

    if (!objectp(binder)) return 0;

    binder_file = file_name(binder);

    // 系统目录下的对象有绑定权限
    if (strsrch(binder_file, "/kernel/") == 0 ||
        strsrch(binder_file, "/system/") == 0) {
        return 1;
    }

    return 0;
}

int valid_read(string path, mixed user, string func)
{
    path = normalize_file_path(path);

    if (!stringp(path) || path == "")
        return 0;

    if (is_internal_request(user))
        return is_internal_source_path(path) || is_runtime_read_path(path);

    /*
     * driver 内部请求（如 E3 v2 重编译事务在 owner 上下文中重载对象）没有
     * LPC 调用者（user==0/undefined），query_request_uid/file 均为空，走
     * is_internal_request 会误拒。这些内核触发场景只能读内部分布式源码
     * 路径；运行时数据路径仍要求显式内部请求，不随此分支放行。
     */
    if (undefinedp(user) || intp(user))
        return is_internal_source_path(path);

    return 0;
}

int valid_write(string path, mixed user, string func)
{
    path = normalize_file_path(path);

    if (!stringp(path) || path == "")
        return 0;

    if (!is_internal_request(user))
        return 0;

    return is_runtime_write_path(path);
}

varargs string get_save_file_name(string file, object ob)
{
    string save_path;

    if (stringp(file) && file != "")
        return normalize_file_path(file);

    if (objectp(ob) && function_exists("query_save_path", ob)) {
        save_path = ob->query_save_path();
        if (stringp(save_path) && save_path != "")
            return normalize_file_path(save_path);
    }

    return 0;
}

/**
 * @brief 对象被销毁时的处理
 * @param ob 被销毁的对象
 *
 * 保护玩家不被销毁
 */
void destruct_env_of(object ob)
{
    object recovery_room;
    mapping moved;

    if (!userp(ob)) return;
    msg_send(ob, "你所存在的空间被毁灭了。\n");

    recovery_room = resolve_recovery_room();
    moved = objectp(recovery_room)
        ? PLAYER_LIFECYCLE_D->recover_player_after_environment_loss(
            ob, recovery_room) : 0;
    if (!mapp(moved) || !moved["ok"]) {
        msg_send(ob, "当前无法将你转移到安全房间，请联系管理员。\n");
        log_file("system/master", sprintf("[%s] destruct_env_of: failed to move %s into recovery room\n",
            ctime(time()), file_name(ob)));
    }
}

/**
 * @brief 返回模拟外部函数对象路径
 * @return simul_efun 对象路径
 *
 * simul_efun 提供额外的 efun 功能，如 tell_room 等
 */
string get_simul_efun() {
    return "/kernel/simul_efun";
}

/**
 * @brief 返回 Driver 启动阶段需要预加载的对象
 * @param load_empty Driver 兼容参数；当前启动合同不因该参数跳过配置
 * @return Driver 在首个玩家进入前交给 preload() 的对象路径
 *
 * 多核配置属于运行时 service，不在 create() 中直接加载。使用 FluffOS
 * 的 master epilog/preload 生命周期，保持启动顺序，同时不把 service
 * placement 逻辑混入 master 初始化。
 */
string *epilog(int load_empty)
{
    return ({ MULTICORE_CONFIG_D });
}

/**
 * @brief 加载 Driver 启动清单中的对象
 * @param filename epilog() 返回的对象路径
 */
void preload(string filename)
{
    object loaded;
    mixed err;

    if (filename != MULTICORE_CONFIG_D) {
        log_file("system/master", sprintf("[%s] rejected unexpected preload object: %O\n",
            ctime(time()), filename));
        return;
    }

    err = catch(loaded = load_object(filename));
    if (err || !objectp(loaded))
        log_file("system/master", sprintf("[%s] failed to preload multicore config: %O\n",
            ctime(time()), err));
}

/**
 * @brief 创建时初始化
 */
void create()
{
    // 确保日志目录存在
    if (file_size(LOG_DIR) == -1) {
        mkdir(LOG_DIR);
    }
}

// 驱动通过 apply 调用时保留 previous_object；只允许已鉴权的更新服务调用原语。
int valid_recompile_object(object target)
{
    object updater;

    updater = find_object("/daemons/services/system/update_d");
    return objectp(target) && objectp(updater) && previous_object() == updater;
}
