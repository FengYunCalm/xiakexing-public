/**
 * @file    look_room.c
 * @brief   查看房间模块 - Room Viewing



 */

#pragma strict_types

#include <system/ansi.h>
#include <game/room.h>
#include <system/services.h>
#include <framework/message_facade.h>

#define LOOK_PHASE_TAIL_NS 25000000

private string get_object_display_name(object ob, string fallback);
private string get_object_short_name(object ob, string fallback);

private string get_inventory_display_name(object ob)
{
    return call_other("/lib/std/utils/util_player", "get_object_short_name", ob, "");
}

private int should_hide_room_item_by_chest_id(string chest_id, object me)
{
    if (!objectp(me) || !stringp(chest_id) || chest_id == "")
        return 0;
    return EXPLORE_REWARD_D->query_chest_open_count(me, chest_id) > 0;
}

private int should_hide_room_item_for_player(object item, object me)
{
    string chest_id;

    if (!objectp(item) || !objectp(me))
        return 0;
    chest_id = item->query("exploration_chest_id") || item->query("chest_id") || "";
    return should_hide_room_item_by_chest_id(chest_id, me);
}

string look_room_inventory(object me, object env, mapping room_view);

/**
 * @brief 查看房间
 */
void look_room(object me, object env, mapping response_context)
{
    mapping room_view;
    mapping room_view_timing;
    mapping payload;
    mapping exits_map;
    string str, exits, title, room_long, *dirs;
    int look_started_at;
    int room_view_started_at;
    int room_view_finished_at;
    int read_summary_started_at;
    int read_summary_finished_at;
    int render_started_at;
    int render_finished_at;
    int emit_started_at;
    int emit_finished_at;
    int total_elapsed_ns;

    if (!me || !env) return;

    look_started_at = perf_counter_ns();
    room_view_started_at = perf_counter_ns();
    room_view_timing = ([ ]);
    room_view = ROOM_VIEW_D->query_room_view_with_timing(me, env, room_view_timing);
    room_view_finished_at = perf_counter_ns();
    read_summary_started_at = perf_counter_ns();
    read_summary_finished_at = perf_counter_ns();
    render_started_at = perf_counter_ns();
    title = mapp(room_view) ? (room_view["title"] || "未知地点") :
        call_other("/lib/std/utils/util_player", "get_object_short_name", env, "未知地点");

    str = HIC + title + NOR "\n";

    // 房间描述 + 昼夜氛围（时间系统挂载点）
    // 统一经 query_long()（content 房间从 descriptions 真源读取），
    // 只查询一次；描述与氛围拼接边界补换行（内容文案不携带格式控制符）。
    room_long = env->query_long();
    if (stringp(room_long) && room_long != "")
        str += room_long + (room_long[<1..] == "\n" ? "" : "\n");
    str += TIME_D->query_ambience_line_for(([
        "outdoors": env->query("outdoors") || 0,
        "title": title,
    ]));

    // 显示物品
    str += look_room_inventory(me, env, room_view);

    // 显示出口
    exits_map = mapp(room_view) ? room_view["exits"] : env->query("exits");
    dirs = mapp(exits_map) ? keys(exits_map) : ({});
    if (sizeof(dirs) > 0) {
        exits = implode(dirs, "、");
        str += "\n" HIY "【出口】" NOR " " + exits + "\n";
    } else {
        str += "\n" HIY "【出口】" NOR " 无明显的出口\n";
    }

    render_finished_at = perf_counter_ns();
    emit_started_at = perf_counter_ns();
    payload = ([
        "kind": "room_view",
        "room": file_name(env),
        "title": title,
        "read_side_worker": 0,
        "read_side_worker_state": "main_thread",
    ]);
    if (me->query_temp("loadtest/move_trace_run_id")
        && arrayp(me->query_temp("loadtest/move_trace")))
        payload["loadtest_move_trace"] = me->query_temp("loadtest/move_trace");
    msg_event(me, str, ([
        "channel": "main",
        "intent": "append",
        "priority": "normal",
        "reliability": "important",
        "display_mode": "paced",
        "command": "look",
        "response_context": response_context,
        "payload": payload,
    ]));
    emit_finished_at = perf_counter_ns();
    total_elapsed_ns = emit_finished_at - look_started_at;
    if (total_elapsed_ns >= LOOK_PHASE_TAIL_NS)
        GATEWAY_SESSION_D->record_look_phase_tail(([
            "total": total_elapsed_ns,
            "room_view": room_view_finished_at - room_view_started_at,
            "read_summary": read_summary_finished_at - read_summary_started_at,
            "render": render_finished_at - render_started_at,
            "emit": emit_finished_at - emit_started_at,
            "room_view_ensure": room_view_timing["ensure"],
            "room_view_ensure_owner_route": room_view_timing["ensure_owner_route"],
            "room_view_ensure_gate": room_view_timing["ensure_gate"],
            "room_view_ensure_entities": room_view_timing["ensure_entities"],
            "room_view_shared": room_view_timing["shared"],
            "room_view_player_filter": room_view_timing["player_filter"],
            "room_view_quest_projection": room_view_timing["quest_projection"],
            "room_view_finalize": room_view_timing["finalize"],
        ]));
}

/**
 * @brief 查看房间内物品
 */
string look_room_inventory(object me, object env, mapping room_view)
{
    object *obs;
    mixed *livings;
    mixed living_entry;
    string str = "";
    string chest_id;
    string item_str;
    string *item_chest_ids;
    string *item_display_names;
    string *npc_display_names;
    string desc;
    int i;
    int __sizeof_livings, __sizeof_obs;
    int truncated_livings;

    if (!mapp(room_view))
        room_view = ROOM_VIEW_D->query_room_view(me, env);
    livings = mapp(room_view) && arrayp(room_view["npcs"]) ? room_view["npcs"] : ({});
    npc_display_names = mapp(room_view) && arrayp(room_view["npc_display_names"])
        ? room_view["npc_display_names"]
        : ({});
    obs = mapp(room_view) && arrayp(room_view["items"]) ? room_view["items"] : ({});
    item_display_names = mapp(room_view) && arrayp(room_view["item_display_names"])
        ? room_view["item_display_names"]
        : ({});
    item_chest_ids = mapp(room_view) && arrayp(room_view["item_chest_ids"])
        ? room_view["item_chest_ids"]
        : ({});

    // 显示人物
    if (sizeof(livings) > 0) {
        str += "\n" HIC "【人物】" NOR "\n";
        __sizeof_livings = sizeof(livings);
        for (i = 0; i < __sizeof_livings; i++) {
            living_entry = livings[i];
            if (i < sizeof(npc_display_names) && stringp(npc_display_names[i])
                && npc_display_names[i] != "")
                desc = npc_display_names[i];
            else if (objectp(living_entry))
                desc = get_inventory_display_name(living_entry);
            else if (mapp(living_entry))
                desc = living_entry["name"] || living_entry["id"] || "";
            else
                desc = "";
            if (!stringp(desc) || desc == "")
                continue;
            str += "  " + desc + "\n";
        }
        truncated_livings = intp(room_view["npcs_truncated"]) ? room_view["npcs_truncated"] : 0;
        if (truncated_livings > 0)
            str += sprintf("  另有 %d 人在附近。\n", truncated_livings);
    }

    // 显示物品
    if (sizeof(obs) > 0) {
        item_str = "";
        __sizeof_obs = sizeof(obs);
        for (i = 0; i < __sizeof_obs; i++) {
            chest_id = i < sizeof(item_chest_ids) ? item_chest_ids[i] : "";
            if (chest_id == "") {
                if (should_hide_room_item_for_player(obs[i], me))
                    continue;
            } else if (should_hide_room_item_by_chest_id(chest_id, me))
                continue;
            desc = i < sizeof(item_display_names) ? item_display_names[i] : "";
            if (!stringp(desc) || desc == "")
                desc = get_inventory_display_name(obs[i]);
            if (!stringp(desc) || desc == "")
                continue;
            item_str += "  " + desc + "\n";
        }
        if (item_str != "")
            str += "\n" HIG "【物品】" NOR "\n" + item_str;
    }

    return str;
}
