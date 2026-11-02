/**
 * @file    social_d.c
 * @brief   社交聚合门面（aggregate facade）



 *
 * @note
 * aggregate facade，统一提供社交子服务加载入口，避免旧模块索引直接指向单一子服务。
 * team/guild/chat/mail 的领域状态仍各自归对应子服务真源持有；
 * 当前对外只保留子服务 loader 与队伍聚合操作，不应继续长成第二套社交业务真源。
 */

#pragma strict_types
#pragma save_types

#include <framework/message_facade.h>

inherit "/lib/framework/daemon";

#define TEAM_SERVICE  "/daemons/services/social/team_d"
#define GUILD_SERVICE "/daemons/services/social/guild_d"
#define CHAT_SERVICE  "/daemons/services/social/chat_d"
#define MAIL_SERVICE  "/daemons/services/social/mail_d"

private nosave mapping _service_cache = ([]);

private object resolve_social_subservice(string path)
{
    object service;

    if (!stringp(path) || path == "")
        return 0;

    if (mapp(_service_cache) && objectp(_service_cache[path]))
        return _service_cache[path];

    service = find_object(path);
    if (!objectp(service))
        service = load_object(path);

    if (objectp(service))
        _service_cache[path] = service;

    return service;
}
object load_team_service()
{
    return resolve_social_subservice(TEAM_SERVICE);
}

object load_guild_service()
{
    return resolve_social_subservice(GUILD_SERVICE);
}

object load_chat_service()
{
    return resolve_social_subservice(CHAT_SERVICE);
}

object load_mail_service()
{
    return resolve_social_subservice(MAIL_SERVICE);
}

private int set_team_membership(object leader, object target)
{
    object service;

    service = load_team_service();
    return objectp(service) ? service->set_team_member(leader, target) : 0;
}

int create_team(object leader, object target)
{
    return set_team_membership(leader, target);
}

int join_team(object leader, object target)
{
    return set_team_membership(leader, target);
}

int leave_team(object player)
{
    object service;

    service = load_team_service();
    return objectp(service) ? service->leave_team(player) : 0;
}

void create()
{
    ::create();

    if (!mapp(_service_cache))
        _service_cache = ([]);
}
