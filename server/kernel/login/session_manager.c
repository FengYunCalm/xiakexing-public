/**
 * @file    session_manager.c
 * @brief   会话管理模块 - Session Manager
 */

#pragma strict_types

#include <system/globals.h>

mapping query_sessions();
int query_session_timeout();

#define SESSION_CLEANUP_INTERVAL 600

void schedule_session_cleanup()
{
    remove_call_out("cleanup_expired_sessions");
    call_out("cleanup_expired_sessions", SESSION_CLEANUP_INTERVAL);
}

private string query_object_session_id(object ob)
{
    mixed session_id;

    if (!objectp(ob) || !function_exists("query_temp", ob))
        return "";

    session_id = ob->query_temp("session_id");
    return stringp(session_id) ? session_id : "";
}

private string snapshot_session_account_id(object user)
{
    mixed account_id;
    mixed err;

    if (!objectp(user))
        return "";

    if (function_exists("query_account_id", user)) {
        err = catch(account_id = user->query_account_id());
        if (!err && stringp(account_id) && account_id != "")
            return account_id;
    }

    if (function_exists("query", user)) {
        err = catch(account_id = user->query("account_id"));
        if (!err && stringp(account_id) && account_id != "")
            return account_id;
    }

    return get_object_id(user);
}

private string generate_session_id()
{
    string chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    string id = "";
    int i;

    for (i = 0; i < 32; i++) {
        id += chars[random(62)];
    }

    return id;
}

mapping create_session(object user)
{
    mapping session;
    string session_id;
    mapping sessions;

    if (!objectp(user))
        return 0;

    /*
     * query_sessions() 返回宿主对象持有的会话表引用。
     * 这里直接写入该 mapping，由门面对象统一拥有生命周期。
     */
    sessions = query_sessions();
    if (!mapp(sessions))
        return 0;

    do {
        session_id = generate_session_id();
    } while (mapp(sessions[session_id]));

    session = ([
        "id": session_id,
        "user": user,
        "account_id": snapshot_session_account_id(user),
        "login_time": time(),
        "last_active": time(),
        "ip": query_ip_number(user),
        "state": "active"
    ]);

    sessions[session_id] = session;

    return session;
}

void destroy_session(string session_id)
{
    if (!stringp(session_id))
        return;

    map_delete(query_sessions(), session_id);
}

mapping get_session(string session_id)
{
    if (!stringp(session_id))
        return 0;

    return query_sessions()[session_id];
}

int can_handoff_session(object user, object login_ob)
{
    string user_session_id;
    mapping session;

    if (!objectp(user) || !objectp(login_ob))
        return 0;

    if (!interactive(login_ob))
        return 0;

    user_session_id = query_object_session_id(user);
    if (user_session_id == "")
        return 0;

    session = get_session(user_session_id);
    if (!mapp(session))
        return 0;

    return session["user"] == user && session["state"] == "active";
}

void update_session_activity(string session_id)
{
    mapping session;

    session = get_session(session_id);
    if (!mapp(session))
        return;

    session["last_active"] = time();
}

void cleanup_expired_sessions()
{
    string session_id;
    mapping session;
    int current_time;
    int session_timeout;
    string *to_remove;
    int __sizeof_remove;
    int i;
    mapping sessions;

    sessions = query_sessions();
    if (!mapp(sessions))
        sessions = ([]);

    current_time = time();
    session_timeout = query_session_timeout();
    to_remove = ({});

    foreach (session_id, session in sessions)
    {
        if (!mapp(session))
        {
            to_remove += ({ session_id });
            continue;
        }

        // 这里只清理会话登记，不负责析构 user，对象生命周期仍由上层登录/玩家流转控制。
        if (current_time - session["last_active"] > session_timeout)
        {
            to_remove += ({ session_id });
        }
    }

    __sizeof_remove = sizeof(to_remove);
    for (i = 0; i < __sizeof_remove; i++)
    {
        map_delete(sessions, to_remove[i]);
    }

    schedule_session_cleanup();
}
