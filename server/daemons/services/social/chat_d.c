/**
 * @file    chat_d.c
 * @brief   聊天守护进程门面



 *
 * @par Description:
 * 聊天系统 canonical path。
 * 当前仅保留稳定 daemon 路径与最小频道元数据/冷却能力；
 * 房间/私聊/广播真链已经收口在命令层与 `simul_efun_message.c`。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/constants.h>
#include <system/ansi.h>
#include <system/errors.h>
#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/framework/daemon";

#define CHANNEL_TYPE_PUBLIC     0
#define CHANNEL_TYPE_PRIVATE    1
#define CHANNEL_TYPE_GUILD      2
#define CHANNEL_TYPE_TEAM       3
#define CHANNEL_TYPE_SYSTEM     4

#ifndef CHANNEL_HISTORY_SIZE
#define CHANNEL_HISTORY_SIZE    50
#endif

#ifndef MAX_CHANNEL_NAME_LEN
#define MAX_CHANNEL_NAME_LEN    20
#endif

#define MSG_COOLDOWN            EMOTE_COOLDOWN
#define CHAT_COOLDOWN_CACHE_LIMIT 4096
#define CHAT_COOLDOWN_PRUNE_BUDGET 16

nosave mapping _channels;
nosave mapping _msg_cooldown;
nosave string *_cooldown_prune_keys;
nosave int _cooldown_prune_index;

void init_default_channels();
int create_channel(string channel_id, int channel_type, string channel_name, string channel_desc, string channel_color);
string *query_all_channels();
mapping query_channel_info(string channel);

private string ensure_player_chat_id(object player)
{
    string player_id;

    if (!objectp(player))
        return "";

    if (function_exists("query_account_id", player)) {
        player_id = player->query_account_id() || "";
        if (stringp(player_id) && player_id != "")
            return player_id;
    }

    return get_object_id(player);
}

void log_info(string msg)
{
    log_file("system/chat", sprintf("[INFO] %s\n", msg));
}

void setup()
{
    _channels = ([]);
    _msg_cooldown = ([]);
    _cooldown_prune_keys = ({ });
    _cooldown_prune_index = 0;

    init_default_channels();

    log_info("Chat daemon v3.0 Facade initialized");
}

void create()
{
    ::create();
    setup();
}

void init_default_channels()
{
    create_channel("chat", CHANNEL_TYPE_PUBLIC, "闲聊", "大家一起来聊天", HIC);

    create_channel("newbie", CHANNEL_TYPE_PUBLIC, "新手", "新手指南和帮助", HIG);
}

int create_channel(string channel_id, int channel_type, string channel_name, string channel_desc, string channel_color)
{
    if (!stringp(channel_id) || channel_id == "")
        return 0;
    if (strlen(channel_id) > MAX_CHANNEL_NAME_LEN)
        return 0;
    if (!stringp(channel_name) || channel_name == "")
        channel_name = channel_id;
    if (!stringp(channel_desc))
        channel_desc = "";
    if (!stringp(channel_color) || channel_color == "")
        channel_color = NOR;

    _channels[channel_id] = ([
        "id": channel_id,
        "type": channel_type,
        "name": channel_name,
        "description": channel_desc,
        "color": channel_color
    ]);

    return 1;
}

/**
 * @brief 查询所有频道
 * @return 频道名称数组
 */
string *query_all_channels()
{
    return keys(_channels);
}

/**
 * @brief 查询频道信息
 * @param channel 频道名
 * @return 频道数据映射
 */
mapping query_channel_info(string channel)
{
    if (!stringp(channel))
        return 0;
    return copy(_channels[channel]);
}

/**
 * @brief 检查频道是否存在
 * @param channel 频道名
 * @return 1=存在, 0=不存在
 */
int channel_exists(string channel)
{
    return stringp(channel) && mapp(_channels[channel]);
}

mapping query_chat_runtime_snapshot()
{
    return ([
        "channel_count": sizeof(keys(_channels || ([]))),
        "channels": query_all_channels(),
        "cooldown_count": sizeof(keys(_msg_cooldown || ([]))),
        "cooldown_seconds": MSG_COOLDOWN,
        "cooldown_cache_limit": CHAT_COOLDOWN_CACHE_LIMIT,
    ]);
}

/**
 * @brief 过滤消息内容
 * @param message 原始消息
 * @return 过滤后的消息
 */
string sanitize_chat_message(string message)
{
    string filtered;

    if (!stringp(message))
        return "";

    filtered = trim(message);

    if (strlen(filtered) > MAX_CHAT_LENGTH)
        filtered = filtered[0..MAX_CHAT_LENGTH-1];

    return filtered;
}

private void prune_expired_cooldowns(int now)
{
    string id;
    int inspected;
    int last_time;

    if (!arrayp(_cooldown_prune_keys)
        || _cooldown_prune_index >= sizeof(_cooldown_prune_keys)) {
        _cooldown_prune_keys = keys(_msg_cooldown || ([]));
        _cooldown_prune_index = 0;
    }
    while (_cooldown_prune_index < sizeof(_cooldown_prune_keys)
        && inspected++ < CHAT_COOLDOWN_PRUNE_BUDGET) {
        id = _cooldown_prune_keys[_cooldown_prune_index++];
        last_time = _msg_cooldown[id];
        if (!intp(last_time) || now - last_time >= MSG_COOLDOWN)
            map_delete(_msg_cooldown, id);
    }
}

/**
 * @brief 只读检查消息冷却，不占用本次发送额度。
 */
int can_send_message(object player)
{
    int last_time;
    int now;
    string id;

    if (!objectp(player))
        return 0;

    id = ensure_player_chat_id(player);
    if (!stringp(id) || id == "")
        return 0;

    now = time();
    prune_expired_cooldowns(now);
    last_time = _msg_cooldown[id];
    if (last_time && now - last_time < MSG_COOLDOWN)
        return 0;
    if (last_time)
        map_delete(_msg_cooldown, id);
    return 1;
}

/**
 * @brief 发送成功后占用消息冷却额度。
 */
int arm_message_cooldown(object player)
{
    string id;
    string candidate_id;
    string oldest_id;
    int now;
    int oldest_at;
    int candidate_at;
    int last_time;

    oldest_id = "";
    if (!objectp(player))
        return 0;
    id = ensure_player_chat_id(player);
    if (!stringp(id) || id == "")
        return 0;

    now = time();
    prune_expired_cooldowns(now);
    if (!_msg_cooldown[id]
        && sizeof(keys(_msg_cooldown || ([]))) >= CHAT_COOLDOWN_CACHE_LIMIT) {
        oldest_at = now + 1;
        foreach (candidate_id in keys(_msg_cooldown || ([]))) {
            last_time = _msg_cooldown[candidate_id];
            candidate_at = intp(last_time) ? last_time : 0;
            if (candidate_at < oldest_at) {
                oldest_at = candidate_at;
                oldest_id = candidate_id;
            }
        }
        if (oldest_id != "")
            map_delete(_msg_cooldown, oldest_id);
    }
    _msg_cooldown[id] = now;
    return 1;
}

/**
 * @brief 兼容旧调用名：只读检查，必须由成功发送路径显式 arm。
 */
int check_message_cooldown(object player)
{
    return can_send_message(player);
}

mapping query_player_chat_state(object player)
{
    int last_time;
    int remaining;
    int now;
    string player_id;

    player_id = ensure_player_chat_id(player);
    if (!stringp(player_id) || player_id == "")
        return ([
            "player_id": "",
            "last_message_at": 0,
            "cooldown_remaining": 0,
            "can_send": 0,
        ]);

    now = time();
    prune_expired_cooldowns(now);
    last_time = _msg_cooldown[player_id] || 0;
    remaining = 0;
    if (last_time && now - last_time < MSG_COOLDOWN)
        remaining = MSG_COOLDOWN - (now - last_time);
    else if (last_time) {
        map_delete(_msg_cooldown, player_id);
        last_time = 0;
    }
    if (remaining < 0)
        remaining = 0;

    return ([
        "player_id": player_id,
        "last_message_at": last_time,
        "cooldown_remaining": remaining,
        "can_send": remaining <= 0 ? 1 : 0,
    ]);
}

int clear_message_cooldown(object player)
{
    string player_id;

    player_id = ensure_player_chat_id(player);
    if (!stringp(player_id) || player_id == "")
        return 0;

    map_delete(_msg_cooldown, player_id);
    return 1;
}

mapping handle_player_logout(object player)
{
    if (!objectp(player))
        return ([ "ok": 0, "error": "player unavailable" ]);
    clear_message_cooldown(player);
    return ([ "ok": 1, "state": "chat_cooldown_released" ]);
}
