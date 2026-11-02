#pragma strict_types
#pragma save_types

#include <framework/message_facade.h>

inherit "/lib/base/content_npc";

nosave string _event_id;
nosave mapping _event;

void create()
{
    ::create();
    set("world_event_object", 1);
    set("functions", ({ "talk" }));
    set("no_fight", 1);
    set("no_attack", 1);
}

int apply_event_context(string event_id, mapping event)
{
    mixed *ids;

    if (!stringp(event_id) || event_id == "" || !mapp(event))
        return 0;

    _event_id = event_id;
    _event = copy(event);
    set("world_event_object", 1);
    set_name(stringp(event["name"]) && event["name"] != ""
        ? event["name"] : event_id);
    ids = ({ event_id });
    if (stringp(event["present_id"]) && event["present_id"] != "")
        ids += ({ event["present_id"] });
    if (stringp(event["name"]) && event["name"] != "")
        ids += ({ event["name"] });
    set("id", ids);
    set("npc_type", stringp(event["npc_type"])
        ? event["npc_type"] : "encounter");
    set("role", stringp(event["role"]) ? event["role"] : "encounter");
    set("long", stringp(event["long"]) && event["long"] != ""
        ? event["long"] : "一个只在特定时辰出现的陌生人。\n");
    if (function_exists("apply_content_ask_topics", this_object()))
        this_object()->apply_content_ask_topics(
            mapp(event["ordinary_topics"]) ? event["ordinary_topics"] : ([ ]),
            mapp(event["story_topics"]) ? event["story_topics"] : ([ ]));
    set("functions", ({ "talk", "ask" }));
    set("no_fight", 1);
    set("no_attack", 1);
    return 1;
}

int do_talk(string arg)
{
    object player;
    string text;
    mixed *topics;

    player = this_player();
    if (!objectp(player))
        return 1;
    topics = function_exists("query_client_ask_topics", this_object())
        ? this_object()->query_client_ask_topics(player) : ({ });
    if (arrayp(topics) && sizeof(topics) > 0)
        return ::do_talk(arg);
    text = mapp(_event) && stringp(_event["talk_text"])
        ? _event["talk_text"] : "这个人只在特定时辰出现。\n";
    msg_send(player, text);
    return 1;
}
