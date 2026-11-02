/**
 * @file    session_auth.c
 * @brief   会话认证模块 - Session Authentication
 */

#pragma strict_types

#include <system/globals.h>
#include <system/ansi.h>

mapping query_login_attempts();
int query_max_attempts();

string generate_session_id()
{
    string chars;
    string id;
    int i;

    chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    id = "";

    for (i = 0; i < 32; i++)
    {
        id += chars[random(62)];
    }

    return id;
}

int validate_login_attempts(string ip)
{
    int attempts;
    mapping login_attempts;

    if (!stringp(ip))
        return 0;

    login_attempts = query_login_attempts();
    if (!mapp(login_attempts))
        return 0;

    attempts = login_attempts[ip];
    if (!intp(attempts))
        attempts = 0;

    if (attempts >= query_max_attempts())
    {
        return 0;  // 超过最大尝试次数
    }

    return 1;
}

void record_login_attempt(string ip)
{
    mapping login_attempts;

    if (!stringp(ip))
        return;

    login_attempts = query_login_attempts();
    if (!mapp(login_attempts))
        return;

    if (!intp(login_attempts[ip]))
        login_attempts[ip] = 0;

    login_attempts[ip]++;
}

void reset_login_attempts(string ip)
{
    mapping login_attempts;

    if (!stringp(ip))
        return;

    login_attempts = query_login_attempts();
    if (!mapp(login_attempts))
        return;

    map_delete(login_attempts, ip);
}
