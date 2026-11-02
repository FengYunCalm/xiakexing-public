/**
 * @file    update_d.c
 * @brief   管理员热更新：授权后由服务完成原子程序替换，不销毁旧对象。
 */

#pragma strict_types

#include <system/ansi.h>
#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/framework/daemon";

private string query_update_actor_id(object me)
{
    return objectp(me) ? (get_object_id(me) || "system") : "system";
}

private string resolve_file_path(object me, string arg)
{
    string file;
    object env;

    if (arg == ".")
        arg = "here";
    if (arg == "last")
        return me->query_temp("update/last_file");
    if (arg == "here") {
        env = environment(me);
        if (!objectp(env)) {
            msg_send(me, "你不在任何环境中。\n");
            return 0;
        }
        return base_name(env) + ".c";
    }
    file = arg;
    if (file[0] != '/')
        file = "/" + file;
    if (strlen(file) < 2 || file[strlen(file) - 2..] != ".c")
        file += ".c";
    return file;
}

private void report_update(string actor_path, string actor_id, string file, string result)
{
    object actor;

    log_file("update", sprintf("[%s] %s 更新 %s：%s\n", ctime(time()), actor_id, file, result));
    actor = find_object(actor_path);
    if (objectp(actor) && query_update_actor_id(actor) == actor_id)
        msg_send(actor, result + "\n");
}

private string query_reload_blocker(object target)
{
    object member;
    object occupant;

    if (target == master() || target == this_object()
        || target == find_object("/kernel/simul_efun"))
        return "此目标请通过标准重启入口更新，不执行热更。";
    if (function_exists("query_exits", target)) {
        foreach (member in children(base_name(target))) {
            foreach (occupant in all_inventory(member)) {
                if (objectp(occupant) && interactive(occupant))
                    return "对象族的房间中有在线玩家，不执行热更。";
            }
        }
    }
    return 0;
}

private void do_atomic_reload(string actor_path, string actor_id, string file)
{
    object target;
    string blocker;
    mixed err;
    int replaced;

    target = find_object(file);
    if (!objectp(target)) {
        report_update(actor_path, actor_id, file, "热更新失败：原目标已不存在，未创建替代对象。");
        return;
    }
    blocker = query_reload_blocker(target);
    if (stringp(blocker)) {
        report_update(actor_path, actor_id, file, "热更新失败：" + blocker);
        return;
    }
    err = catch(replaced = recompile_object(target));
    if (err || replaced < 1) {
        report_update(actor_path, actor_id, file,
            sprintf("热更新失败，旧对象保留：%O", err || "没有替换任何对象"));
        return;
    }
    report_update(actor_path, actor_id, file,
        sprintf("热更新成功，已原子替换 %d 个对象的程序。", replaced));
}

int request_update(object me, string arg)
{
    string file;
    string actor_path;
    string actor_id;
    string blocker;
    object target;
    mixed err;

    if (!objectp(me))
        return 0;
    if (!CONFIG_D->is_admin_player(me)) {
        msg_send(me, "你没有权限执行此命令。\n");
        return 0;
    }
    if (!stringp(arg) || arg == "")
        arg = "last";
    file = resolve_file_path(me, arg);
    if (!stringp(file) || file == "") {
        msg_send(me, "指令格式: update <文件路径|here>\n");
        return 1;
    }
    if (file[0] != '/' || strsrch(file, "..") >= 0 || strsrch(file, "//") >= 0
        || file_size(file) <= 0) {
        msg_send(me, "更新路径无效或文件不存在。\n");
        return 1;
    }
    actor_path = file_name(me);
    actor_id = query_update_actor_id(me);
    target = find_object(file);
    blocker = objectp(target) ? query_reload_blocker(target) : 0;
    if (stringp(blocker)) {
        msg_send(me, blocker + "\n");
        return 1;
    }
    me->set_temp("update/last_file", file);
    if (!objectp(target)) {
        err = catch(target = load_object(file));
        report_update(actor_path, actor_id, file, err || !objectp(target)
            ? sprintf("加载失败：%O", err || "没有加载对象") : "加载成功！");
        return 1;
    }
    call_out("do_atomic_reload", 0, actor_path, actor_id, file);
    msg_send(me, "热更新请求已受理。\n");
    return 1;
}
