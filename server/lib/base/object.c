/**
 * @file    object.c
 * @brief   基础对象基类



 *
 * @par Description:
 * 游戏世界所有对象的基类，统一提供数据、名称、移动与清理基础能力。
 */

#pragma strict_types

#include <system/globals.h>
#include <system/constants.h>
#include <system/services.h>

#undef is_player

/* data.c 必须首先继承，因为 name.c 和 cleanup.c 依赖它的函数 */
inherit "/kernel/framework/data";
inherit "/kernel/framework/name";
inherit "/kernel/framework/move";
inherit "/kernel/framework/cleanup";

private int message_facade_uses_xk(object ob)
{
    mixed version;

    if (!objectp(ob) || !interactive(ob))
        return 0;
    if (ob->query_temp("client/plain_text_only"))
        return 0;
    version = ob->query_temp("xk_protocol_version");
    return stringp(version) && version != "";
}

private int message_facade_has_visible_text(string msg)
{
    return stringp(msg) && trim(msg) != "";
}

/*
 * object 基类拥有统一消息 wrapper，XK 客户端走 MSGE，纯文本客户端走旧文本。
 * 已经手动发专属结构包的对象路径应调用 msg_send_legacy() 避免重复 main 事件。
 */
void msg_send_legacy(object ob, string msg)
{
    call_other("/kernel/simul_efun", "msg_send_raw", ob, msg);
}

varargs void msg_send(object ob, string msg, mapping response_context)
{
    mixed err;
    int result;
    mapping options;

    if (message_facade_uses_xk(ob) && message_facade_has_visible_text(msg)) {
        options = ([
            "channel": "main",
            "intent": "append",
            "priority": "normal",
            "reliability": "important",
            "display_mode": "paced",
            "payload": ([ "kind": "plain_text" ]),
        ]);
        if (undefinedp(response_context)) {
            if (ob == this_player())
                response_context = CLIENT_SYNC_D->capture_command_response_context(ob);
            else
                response_context = 0;
        }
        if (!undefinedp(response_context))
            options["response_context"] = mapp(response_context)
                ? copy(response_context) : response_context;
        err = catch(result = MESSAGE_D->emit_message_event(ob, msg, options));
        /* emit 返回 0 或抛异常时继续回退 legacy 文本，保证玩家可见。 */
        if (!err && result)
            return;
    }
    msg_send_legacy(ob, msg);
}

varargs void msg_to_room(mixed room, string msg, mixed exclude)
{
    object *targets;
    object *excluded;
    object target;

    if (!room || !stringp(msg) || msg == "")
        return;
    if (stringp(room))
        room = load_object(room);
    if (!objectp(room))
        return;

    if (objectp(exclude))
        excluded = ({ exclude });
    else if (arrayp(exclude))
        excluded = filter(exclude, (: objectp($1) :));
    else
        excluded = ({ });

    targets = all_inventory(room);
    foreach (target in targets) {
        if (!objectp(target) || member_array(target, excluded) != -1)
            continue;
        if (!interactive(target) && !living(target) && !function_exists("catch_tell", target))
            continue;
        msg_send(target, msg);
    }
}

varargs void msg_to_room_raw(mixed room, string msg, mixed exclude)
{
    call_other("/kernel/simul_efun", "msg_to_room_raw", room, msg, exclude);
}

void msg_broadcast(string msg)
{
    object target;

    if (!stringp(msg) || msg == "")
        return;
    foreach (target in users()) {
        if (objectp(target) && environment(target))
            msg_send(target, msg);
    }
}

/*
 * XK 协议发送 wrapper：保持业务层不直接碰底层协议发送，
 * 具体状态/房间同步统一收口到 client_sync_d。
 */
varargs void emit_xk_room_snapshot(object player, object room,
    mapping response_context)
{
    /* object 层只传播调用点冻结的上下文；后台刷新默认无关联。 */
    CLIENT_SYNC_D->request_room_snapshot(player, room, response_context);
}

varargs void send_xk_status(object player, mapping response_context)
{
    CLIENT_SYNC_D->request_status(player, response_context);
}

/**
 * @brief 检查是否为对象
 * @return 1 总是返回1表示是对象
 */
int is_object()
{
    return 1;
}

/**
 * @brief 创建时初始化
 */
void create()
{
    /*
     * create 顺序保持与 inherit 依赖一致，先让 data 建好宿主状态，
     * 再初始化依赖它的名称与清理能力。
     */
    data::create();
    name::create();
    cleanup::create();
}

private int uses_player_state_owner()
{
    return function_exists("is_player", this_object())
        && this_object()->is_player();
}

private int player_state_transaction_ready()
{
    return uses_player_state_owner()
        && query_temp("player/state_owner_ready")
        && interactive(this_object());
}

private string player_state_set_idempotency_key(string path, mixed value)
{
    mapping state;
    int generation;

    state = PLAYER_STATE_D->query_state(this_object(), path);
    generation = mapp(state) && intp(state["generation"])
        ? state["generation"] : 0;
    return sprintf("player/facade/set/%s/%d/%O", path, generation, value);
}

private string player_state_delete_idempotency_key(string path)
{
    mapping state;
    int generation;

    state = PLAYER_STATE_D->query_state(this_object(), path);
    generation = mapp(state) && intp(state["generation"])
        ? state["generation"] : 0;
    return sprintf("player/facade/delete/%s/%d", path, generation);
}

private string player_state_replace_idempotency_key(mapping state_data)
{
    mapping state;
    int generation;

    state = PLAYER_STATE_D->query_state(this_object(),
        "persistence/player_state_generation");
    generation = mapp(state) && intp(state["generation"])
        ? state["generation"] : 0;
    return sprintf("player/facade/replace/%d/%s", generation,
        hash("sha256", sprintf("%O", state_data)));
}

mixed query(string path)
{
    if (uses_player_state_owner()
        && PLAYER_STATE_D->is_managed_state_path(path))
        return PLAYER_STATE_D->query_state_value(this_object(), path);
    return data::query(path);
}

int set(string path, mixed value)
{
    mapping response;

    if (uses_player_state_owner()
        && PLAYER_STATE_D->is_managed_state_path(path)) {
        if (player_state_transaction_ready())
            response = PLAYER_STATE_D->commit_state_fields(this_object(),
                ([ path: value ]),
                player_state_set_idempotency_key(path, value));
        else
            response = PLAYER_STATE_D->set_state(this_object(), path, value,
                ([ ]));
        return mapp(response) && response["ok"];
    }
    return data::set(path, value);
}

int delete(string path)
{
    mapping response;

    if (uses_player_state_owner()
        && PLAYER_STATE_D->is_managed_state_path(path)) {
        if (player_state_transaction_ready())
            response = PLAYER_STATE_D->commit_state_deletions(this_object(),
                ({ path }), player_state_delete_idempotency_key(path));
        else
            response = PLAYER_STATE_D->delete_state(this_object(), path,
                ([ ]));
        return mapp(response) && response["ok"];
    }
    return data::delete(path);
}

int add(string path, int value)
{
    mapping response;
    int current;
    int next_value;

    if (uses_player_state_owner()
        && PLAYER_STATE_D->is_managed_state_path(path)) {
        current = query(path);
        if (!intp(current))
            current = 0;
        next_value = current + value;
        if (player_state_transaction_ready())
            response = PLAYER_STATE_D->commit_state_fields(this_object(),
                ([ path: next_value ]),
                player_state_set_idempotency_key(path, next_value));
        else
            response = PLAYER_STATE_D->add_state(this_object(), path, value,
                ([ ]));
        return mapp(response) && response["ok"] ? query(path) : current;
    }
    return data::add(path, value);
}

int set_data(mapping data)
{
    mapping response;

    if (uses_player_state_owner()) {
        if (player_state_transaction_ready())
            response = PLAYER_STATE_D->commit_state_data(this_object(), data,
                player_state_replace_idempotency_key(data));
        else
            response = PLAYER_STATE_D->replace_state_data(this_object(), data,
                ([ "full_replace": 1 ]));
        return mapp(response) && response["ok"];
    }
    return data::set_data(data);
}

/**
 * @brief 设置默认属性
 */
void setup()
{
    seteuid(getuid(this_object()));
}

/**
 * @brief 初始化（对象进入环境时调用）
 */
void init()
{
}
