/**
 * @file    util_view.c
 * @brief   房间视图刷新工具 - helper utility



 *
 * @par Description:
 * helper utility，提供非命令场景下的房间视图刷新入口，统一委托 `look_room` 真源。
 * 本文件只负责视图刷新辅助，不承载房间描述真逻辑或协议发送真源。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

private void log_view_helper_failure(string action, string object_path, mixed err)
{
    log_file("system/view", sprintf("[%s] util_view %s failed for %s: %O\n",
        ctime(time()), action, object_path, err));
}

private int should_replace_room_object(object ob)
{
    int hp;
    int max_hp;

    if (!objectp(ob) || interactive(ob))
        return 0;

    if (ob->query_temp("is_dying"))
        return 1;

    hp = ob->query("combat/hp");
    max_hp = ob->query("combat/max_hp");
    return intp(max_hp) && max_hp > 0 && intp(hp) && hp <= 0;
}

private mapping query_room_respawn_deadlines(object env)
{
    mapping deadlines;

    if (!objectp(env))
        return ([]);

    deadlines = env->query_temp("room/respawn_deadlines");
    if (!mapp(deadlines))
        deadlines = ([]);

    return deadlines;
}

void set_room_respawn_deadline(object env, string object_path, int deadline)
{
    mapping deadlines;

    if (!objectp(env) || !stringp(object_path) || object_path == "")
        return;

    deadlines = query_room_respawn_deadlines(env);
    if (!intp(deadline) || deadline <= time())
        map_delete(deadlines, object_path);
    else
        deadlines[object_path] = deadline;

    env->set_temp("room/respawn_deadlines", deadlines);
}

private int query_room_respawn_deadline(object env, string object_path)
{
    mapping deadlines;
    mixed deadline;

    if (!objectp(env) || !stringp(object_path) || object_path == "")
        return 0;

    deadlines = query_room_respawn_deadlines(env);
    deadline = deadlines[object_path];
    if (!intp(deadline) || deadline <= time()) {
        if (deadline)
            set_room_respawn_deadline(env, object_path, 0);
        return 0;
    }

    return deadline;
}

object ensure_room_object(object env, string present_id, string object_path)
{
    object ob;
    mixed err;

    if (!objectp(env) || !stringp(present_id) || present_id == ""
        || !stringp(object_path) || object_path == "")
        return 0;

    ob = present(present_id, env);
    if (objectp(ob)) {
        if (base_name(ob) == object_path && !should_replace_room_object(ob))
            return ob;

        if (base_name(ob) == object_path && environment(ob) == env)
            destruct(ob);
        else
            ob = 0;
    }

    if (query_room_respawn_deadline(env, object_path))
        return 0;

    err = catch(ob = new(object_path));
    if (err || !objectp(ob)) {
        if (err)
            log_view_helper_failure("new", object_path, err);
        return 0;
    }

    err = catch(ob->move(env));
    if (err) {
        log_view_helper_failure("move", object_path, err);
        if (objectp(ob) && !environment(ob))
            destruct(ob);
        return 0;
    }

    if (environment(ob) != env) {
        if (objectp(ob) && !environment(ob))
            destruct(ob);
        return 0;
    }

    set_room_respawn_deadline(env, object_path, 0);

    return ob;
}

mixed *ensure_room_object_count(object env, string object_path,
    string entity_id, int min_count)
{
    object *inv;
    object ob;
    mixed *instances;
    mapping instance;
    mixed err;

    instances = ({ });
    if (!objectp(env) || !stringp(object_path) || object_path == ""
        || !stringp(entity_id) || entity_id == "" || min_count <= 0)
        return instances;

    inv = all_inventory(env);
    foreach (ob in inv) {
        if (!objectp(ob) || base_name(ob) != object_path
            || ob->query_temp("world_lifecycle/content_entity_id") != entity_id)
            continue;

        if (should_replace_room_object(ob)) {
            if (environment(ob) == env)
                destruct(ob);
            continue;
        }

        instances += ({ ([ "object": ob, "created": 0 ]) });
    }

    if (sizeof(instances) < min_count
        && query_room_respawn_deadline(env, object_path))
        return instances;

    while (sizeof(instances) < min_count) {
        err = catch(ob = new(object_path));
        if (err || !objectp(ob)) {
            if (err)
                log_view_helper_failure("new", object_path, err);
            break;
        }

        err = catch(ob->move(env));
        if (err) {
            log_view_helper_failure("move", object_path, err);
            if (objectp(ob) && !environment(ob))
                destruct(ob);
            break;
        }

        if (environment(ob) != env) {
            if (objectp(ob) && !environment(ob))
                destruct(ob);
            break;
        }

        set_room_respawn_deadline(env, object_path, 0);
        instances += ({ ([ "object": ob, "created": 1 ]) });
    }

    return instances;
}

/**
 * @brief 刷新房间视图
 * @param me 玩家对象
 * @param env 房间对象（可选，缺省时取 environment）
 */
void refresh_room_entities(object env)
{
    mixed err;

    if (!objectp(env))
        return;

    if (WORLD_ENTITY_LIFECYCLE_D->ensure_room_entities(env))
        return;

    if (function_exists("refresh_room_entities", env)) {
        err = catch(call_other(env, "refresh_room_entities"));
        if (err)
            log_view_helper_failure("refresh", file_name(env), err);
        return;
    }

    if (function_exists("ensure_room_entities", env)) {
        err = catch(call_other(env, "ensure_room_entities"));
        if (err)
            log_view_helper_failure("ensure", file_name(env), err);
    }
}

void refresh_room_view(object me, object env)
{
    if (!objectp(me))
        return;

    if (!objectp(env))
        env = environment(me);

    if (!objectp(env))
        return;

    /*
     * 这个工具函数只服务于“纯文本视图刷新”场景。
     * 交互玩家当前由 player->on_move_complete() 直接走 XK 房间同步，
     * 因此这里只给非交互对象或未协商 XK 的兼容对象补 look 视图，
     * 避免把交互玩家直接早退成“什么都不做”。
     */
    if (interactive(me) && stringp(me->query_temp("xk_protocol_version"))
        && me->query_temp("xk_protocol_version") != "")
        return;

    call_other("/cmds/player/look", "look_room", me, env);
}
