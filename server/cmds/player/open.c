#pragma strict_types

#include <framework/message_facade.h>
#include <system/services.h>

inherit "/lib/framework/command";

int main(object me, string arg)
{
    object env;
    object target;
    mapping result;
    string message;
    string chest_id;
    string title;
    mapping response_context;

    if (!objectp(me))
        return 0;
    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    if (!stringp(arg) || arg == "") {
        msg_send(me, "开启什么？\n", response_context);
        return 1;
    }
    env = environment(me);
    if (!objectp(env)) {
        msg_send(me, "你现在不在可探索的地方。\n");
        return 1;
    }
    target = present(arg, env);
    if (!objectp(target)) {
        msg_send(me, "这里没有这个可开启的目标。\n");
        return 1;
    }
    result = EXPLORE_REWARD_D->open_chest(me, target, response_context);
    message = result["message"] || "操作完成。\n";
    chest_id = target->query("exploration_chest_id") || target->query("chest_id") || "";
    title = target->query("name") || target->query("short") || arg;
    msg_event(me, message, ([
        "channel": "main",
        "intent": "append",
        "priority": result["ok"] ? "normal" : "high",
        "reliability": "important",
        "display_mode": "paced",
        "response_context": response_context,
        "payload": ([
            "kind": "chest_open_result",
            "ok": result["ok"] ? 1 : 0,
            "target": arg,
            "chest_id": chest_id,
            "title": title,
        ]),
    ]));
    if (result["ok"] && result["hide_chest"]) {
        CLIENT_SYNC_D->request_room_snapshot(me, env, response_context);
    }
    return 1;
}

int help(object me)
{
    msg_send(me, "命令：open <宝箱>\n");
    return 1;
}
