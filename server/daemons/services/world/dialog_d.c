/**
 * @file    dialog_d.c
 * @brief   对话守护进程,管理对话档案与文本插值。
 */

#pragma strict_types
#pragma save_types

inherit "/lib/framework/daemon";

nosave mapping _profiles;

private string interpolate(string text, object actor, mapping context)
{
    string player_name;

    if (!stringp(text) || text == "")
        return "";

    player_name = objectp(actor) ? (get_object_name(actor) || "侠客") : "侠客";
    text = replace_string(text, "[玩家名]", player_name);

    if (mapp(context) && stringp(context["npc_name"]))
        text = replace_string(text, "[NPC名]", context["npc_name"]);

    return text;
}

private mapping build_default_profiles()
{
    return ([
        "vendor_generic": ([
            "title": "商人交互",
            "keyword_responses": ([]),
            "greeting": ({ }),
            "talk": "[NPC名]和气地说道：先看看货单，想买什么再告诉我。\n",
            "help": "[NPC名]笑道：先看货单，挑中什么再告诉我。\n",
            "quest": "[NPC名]摆手道：我这里主要做买卖，若有委托也会当面说清。\n"
        ]),
        "quest_giver_generic": ([
            "title": "任务交互",
            "keyword_responses": ([]),
            "greeting": ({ }),
            "talk": "[NPC名]抬眼看了看你：想知道我这边有什么差事，就直说。\n",
            "help": "[NPC名]说道：想打听线索或查看委托，直接跟我说明来意就好。\n",
            "quest": "[NPC名]点头道：有些事三言两语说不清，你直接看委托吧。\n"
        ]),
        "guard_generic": ([
            "title": "守卫交互",
            "keyword_responses": ([]),
            "greeting": ({ }),
            "talk": "[NPC名]点了点头：若想了解城中规矩，我可以慢慢讲给你听。\n",
            "help": "[NPC名]说道：先学会观察周围、辨认方向、与人交谈，就能顺着新手流程走下去。\n",
            "quest": "[NPC名]沉声说道：若我这里正好有事要托付，会把委托当面说明。\n"
        ]),
        "guide_generic": ([
            "title": "引导交互",
            "keyword_responses": ([]),
            "greeting": ({ }),
            "talk": "[NPC名]微笑着说：初入江湖不必着急，先多看看、多问问。\n",
            "help": "[NPC名]温声说道：先熟悉观察、移动、交谈和委托这些基础事，再慢慢往外探索。\n",
            "quest": "[NPC名]点头说道：若我这边有适合你的引导内容，会直接列给你看。\n"
        ]),
        "sparring_generic": ([
            "title": "切磋交互",
            "keyword_responses": ([]),
            "greeting": ({ }),
            "talk": "[NPC名]活动了一下筋骨：想练练手的话，先把规矩问清楚。\n",
            "help": "[NPC名]提醒你：切磋前先确认状态，不要把练习当成真正厮杀。\n",
            "quest": "[NPC名]摆手道：我这里主要是切磋，不是发布差事的地方。\n"
        ]),
    ]);
}

void create()
{
    ::create();
    _profiles = build_default_profiles();
}

int register_dialog_profile(string profile_id, mapping profile)
{
    if (!stringp(profile_id) || profile_id == "" || !mapp(profile))
        return 0;
    _profiles[profile_id] = copy(profile);
    return 1;
}

mapping query_dialogue_profile(string profile_id)
{
    if (!stringp(profile_id) || !mapp(_profiles[profile_id]))
        return 0;
    return copy(_profiles[profile_id]);
}

mixed query_dialogue_bucket(string profile_id, string bucket)
{
    mapping profile;

    profile = query_dialogue_profile(profile_id);
    if (!mapp(profile) || !stringp(bucket) || bucket == "")
        return 0;
    return copy(profile[bucket]);
}

string render_dialogue_text(string profile_id, string bucket, object actor, mapping context)
{
    mixed bucket_data;
    string text;

    bucket_data = query_dialogue_bucket(profile_id, bucket);
    if (stringp(bucket_data))
        text = bucket_data;
    else if (arrayp(bucket_data) && sizeof(bucket_data) > 0)
        text = bucket_data[random(sizeof(bucket_data))];
    else
        return 0;

    return interpolate(text, actor, context);
}
