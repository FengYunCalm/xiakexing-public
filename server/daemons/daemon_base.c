/**
 * @file    daemon_base.c
 * @brief   守护进程基类 forwarding shell



 *
 * @par Description:
 * forwarding shell，真实实现统一收口到 `/lib/framework/daemon`。
 * 保留本文件只为兼容旧路径调用，不在这里复制第二套 daemon 基类逻辑。
 */

#pragma strict_types
#pragma save_types

inherit "/lib/framework/daemon";

void create()
{
    ::create();
}

void setup()
{
    ::setup();
}

string query_daemon_name()
{
    return ::query_daemon_name();
}

void log_daemon(string msg)
{
    ::log_daemon(msg);
}

void handle_error(string error)
{
    ::handle_error(error);
}
