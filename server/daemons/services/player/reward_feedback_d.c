/**
 * @file    reward_feedback_d.c
 * @brief   奖励反馈守护进程,格式化奖励条目展示文案。
 */

#pragma strict_types
#pragma save_types

#include <system/ansi.h>
#include <system/services.h>

inherit "/lib/framework/daemon";

private string format_reward_item_entry(mixed entry)
{
    string item_name;
    string item_id;
    int count;

    if (objectp(entry)) {
        item_name = entry->query_name() || entry->query("name") || base_name(entry);
        count = entry->query("count") || entry->query("amount") || 1;
        return sprintf("物品:%s x%d", item_name, count);
    }

    if (mapp(entry)) {
        item_id = entry["template_id"] || entry["item"] || entry["id"] || "";
        if (stringp(item_id) && item_id != "")
            item_name = ITEM_D->get_item_name(item_id);
        if (!stringp(item_name) || item_name == "")
            item_name = entry["item_name"] || item_id || "未知";
        count = intp(entry["count"]) && entry["count"] > 0 ? entry["count"] : 1;
        return sprintf("物品:%s x%d", item_name, count);
    }

    if (stringp(entry) && entry != "")
        return sprintf("物品:%s", ITEM_D->get_item_name(entry));

    return "";
}

mapping create_feedback_state()
{
    return ([
        "gold": 0,
        "potential": 0,
        "exp": 0,
        "sect_contribution": 0,
        "reputation": ([ ]),
        "inventory_items": ({ }),
        "room_items": ({ }),
    ]);
}

mapping merge_applied_feedback(mapping aggregate, mapping applied)
{
    string scope;
    int delta;

    if (!mapp(aggregate))
        aggregate = create_feedback_state();
    if (!mapp(applied))
        return aggregate;

    aggregate["gold"] += intp(applied["gold"]) ? applied["gold"] : 0;
    aggregate["potential"] += intp(applied["potential"]) ? applied["potential"] : 0;
    aggregate["exp"] += intp(applied["exp"]) ? applied["exp"] : 0;
    aggregate["sect_contribution"] += intp(applied["sect_contribution"]) ? applied["sect_contribution"] : 0;
    if (!mapp(aggregate["reputation"]))
        aggregate["reputation"] = ([ ]);
    foreach (scope, delta in mapp(applied["reputation"]) ? applied["reputation"] : ([ ]))
        if (stringp(scope) && scope != "" && intp(delta))
            aggregate["reputation"][scope] = (aggregate["reputation"][scope] || 0) + delta;
    if (arrayp(applied["inventory_items"]))
        aggregate["inventory_items"] += applied["inventory_items"];
    if (arrayp(applied["room_items"]))
        aggregate["room_items"] += applied["room_items"];
    return aggregate;
}

/* L-143：结构化条目投影（kind/label/amount/text/count）——预览
 * （quest_render.format_quest_reward_line）与实发（format_reward_summary）
 * 共用同一字段协议；声望 mapping/附件/多物品不再只在一侧出现。 */
private mapping project_item_entry(mixed entry, string kind)
{
    string item_name;
    string item_id;
    int count;

    if (objectp(entry)) {
        item_name = entry->query_name() || entry->query("name") || base_name(entry);
        count = entry->query("count") || entry->query("amount") || 1;
    } else if (mapp(entry)) {
        item_id = entry["template_id"] || entry["item"] || entry["id"] || "";
        if (stringp(item_id) && item_id != "")
            item_name = ITEM_D->get_item_name(item_id);
        if (!stringp(item_name) || item_name == "")
            item_name = entry["item_name"] || item_id || "未知";
        count = intp(entry["count"]) && entry["count"] > 0 ? entry["count"] : 1;
    } else
        return 0;
    if (!stringp(item_name) || item_name == "")
        return 0;
    return ([ "kind": kind, "label": "物品", "text": item_name, "count": count ]);
}

mixed *project_reward_entries(mapping rewards)
{
    mixed *entries;
    mixed attachment;
    mixed runtime_item;
    mixed item_entry;
    string scope;
    int delta;

    entries = ({ });
    if (!mapp(rewards))
        return entries;

    if ((rewards["gold"] || 0) > 0)
        entries += ({ ([ "kind": "gold", "label": "铜钱",
            "amount": rewards["gold"] ]) });
    if ((rewards["potential"] || 0) > 0)
        entries += ({ ([ "kind": "potential", "label": "潜能",
            "amount": rewards["potential"] ]) });
    if ((rewards["exp"] || 0) > 0)
        entries += ({ ([ "kind": "exp", "label": "经验",
            "amount": rewards["exp"] ]) });
    if ((rewards["sect_contribution"] || 0) > 0)
        entries += ({ ([ "kind": "sect_contribution", "label": "门派贡献",
            "amount": rewards["sect_contribution"] ]) });
    if (intp(rewards["reputation"]) && rewards["reputation"] != 0)
        entries += ({ ([ "kind": "reputation", "label": "声望",
            "amount": rewards["reputation"] ]) });
    foreach (scope, delta in mapp(rewards["reputation"]) ? rewards["reputation"] : ([ ])) {
        if (!stringp(scope) || scope == "" || !intp(delta) || delta == 0)
            continue;
        entries += ({ ([ "kind": "reputation", "label": scope + "声望",
            "amount": delta ]) });
    }
    if (stringp(rewards["title"]) && rewards["title"] != "")
        entries += ({ ([ "kind": "title", "label": "称号",
            "text": rewards["title"] ]) });

    foreach (attachment in arrayp(rewards["attachments"]) ? rewards["attachments"] : ({ })) {
        item_entry = project_item_entry(attachment, "item");
        if (mapp(item_entry))
            entries += ({ item_entry });
    }
    foreach (runtime_item in arrayp(rewards["inventory_items"]) ? rewards["inventory_items"] : ({ })) {
        item_entry = project_item_entry(runtime_item, "item");
        if (mapp(item_entry))
            entries += ({ item_entry });
    }
    foreach (runtime_item in arrayp(rewards["room_items"]) ? rewards["room_items"] : ({ })) {
        item_entry = project_item_entry(runtime_item, "room_item");
        if (mapp(item_entry))
            entries += ({ item_entry });
    }
    if (stringp(rewards["item"]) && rewards["item"] != "") {
        item_entry = project_item_entry(([
            "template_id": rewards["item"],
            "count": intp(rewards["item_count"]) && rewards["item_count"] > 0
                ? rewards["item_count"] : 1,
        ]), "item");
        if (mapp(item_entry))
            entries += ({ item_entry });
    }
    return entries;
}

string format_reward_summary(mapping rewards)
{
    mixed *entries;
    mixed entry;
    string *parts;
    string kind;

    entries = project_reward_entries(rewards);
    parts = ({ });
    foreach (kind in ({ "gold", "potential", "exp", "sect_contribution",
        "reputation", "title", "item", "room_item" })) {
        foreach (entry in entries) {
            if (entry["kind"] != kind)
                continue;
            switch (kind) {
            case "gold":
                parts += ({ sprintf("金钱:%s",
                    MONEY_D->format_money(entry["amount"])) });
                break;
            case "potential":
                parts += ({ sprintf("潜能:%d", entry["amount"]) });
                break;
            case "exp":
                parts += ({ sprintf("经验:%d", entry["amount"]) });
                break;
            case "sect_contribution":
                parts += ({ sprintf("门派贡献:%d", entry["amount"]) });
                break;
            case "reputation":
                parts += ({ sprintf("%s:%d", entry["label"], entry["amount"]) });
                break;
            case "title":
                parts += ({ sprintf("称号:%s", entry["text"]) });
                break;
            case "item":
                parts += ({ sprintf("物品:%s x%d", entry["text"],
                    entry["count"]) });
                break;
            case "room_item":
                parts += ({ sprintf("物品:%s x%d（背包已满，掉落在地上）",
                    entry["text"], entry["count"]) });
                break;
            }
        }
    }

    if (!sizeof(parts))
        return "无附件";
    return implode(parts, "，");
}

string format_mail_claim_success(mapping applied)
{
    return HIG "你领取了邮件附件：" NOR + format_reward_summary(applied) + "\n";
}

string format_mail_claim_all_success(int count, mapping applied)
{
    string summary;

    if (count <= 0)
        return "没有可领取的附件邮件。\n";

    summary = format_reward_summary(applied);
    if (!stringp(summary) || summary == "" || summary == "无附件")
        return HIG + sprintf("已领取 %d 封邮件附件。", count) + NOR + "\n";

    return HIG + sprintf("已领取 %d 封邮件附件：", count) + NOR + summary + "\n";
}

string format_reward_notice(string prefix, mapping rewards)
{
    string summary;

    summary = format_reward_summary(rewards);
    if (!stringp(summary) || summary == "" || summary == "无附件")
        return "";

    if (!stringp(prefix) || prefix == "")
        prefix = "奖励：";

    return prefix + summary + "\n";
}

string *build_reward_gain_lines(mapping applied)
{
    string *lines;
    object item;
    mapping gain;
    string name;
    int count;
    string scope;
    int delta;

    if (!mapp(applied))
        return ({ });

    lines = ({ });
    if (intp(applied["exp"]) && applied["exp"] > 0)
        lines += ({ sprintf("获得经验：%d", applied["exp"]) });
    if (intp(applied["gold"]) && applied["gold"] > 0)
        lines += ({ sprintf("获得金钱：%s", MONEY_D->format_money(applied["gold"])) });
    if (intp(applied["potential"]) && applied["potential"] > 0)
        lines += ({ sprintf("获得潜能：%d", applied["potential"]) });
    if (intp(applied["sect_contribution"]) && applied["sect_contribution"] > 0)
        lines += ({ sprintf("获得门派贡献：%d", applied["sect_contribution"]) });
    foreach (scope, delta in mapp(applied["reputation"]) ? applied["reputation"] : ([ ]))
        if (stringp(scope) && scope != "" && intp(delta) && delta != 0)
            lines += ({ sprintf("获得%s声望：%d", scope, delta) });
    if (stringp(applied["title"]) && applied["title"] != "")
        lines += ({ sprintf("获得称号：%s", applied["title"]) });

    if (arrayp(applied["inventory_item_gains"]) && sizeof(applied["inventory_item_gains"]) > 0) {
        foreach (gain in applied["inventory_item_gains"]) {
            if (!mapp(gain) || !objectp(gain["item"]))
                continue;
            item = gain["item"];
            name = item->query_name() || item->query("name") || base_name(item);
            count = intp(gain["count"]) && gain["count"] > 0 ? gain["count"] : 1;
            lines += ({ sprintf("获得物品：%s%s", name, count > 1 ? (" x" + count) : "") });
        }
    } else if (arrayp(applied["inventory_items"])) {
        foreach (item in applied["inventory_items"]) {
            if (!objectp(item))
                continue;
            name = item->query_name() || item->query("name") || base_name(item);
            count = item->query("count") || item->query("amount") || 1;
            lines += ({ sprintf("获得物品：%s%s", name, count > 1 ? (" x" + count) : "") });
        }
    }

    if (arrayp(applied["room_item_gains"]) && sizeof(applied["room_item_gains"]) > 0) {
        foreach (gain in applied["room_item_gains"]) {
            if (!mapp(gain) || !objectp(gain["item"]))
                continue;
            item = gain["item"];
            name = item->query_name() || item->query("name") || base_name(item);
            count = intp(gain["count"]) && gain["count"] > 0 ? gain["count"] : 1;
            lines += ({ sprintf("获得物品：%s%s（背包已满，掉落在地上）", name,
                count > 1 ? (" x" + count) : "") });
        }
    } else if (arrayp(applied["room_items"])) {
        foreach (item in applied["room_items"]) {
            if (!objectp(item))
                continue;
            name = item->query_name() || item->query("name") || base_name(item);
            count = item->query("count") || item->query("amount") || 1;
            lines += ({ sprintf("获得物品：%s%s（背包已满，掉落在地上）", name,
                count > 1 ? (" x" + count) : "") });
        }
    }

    return lines;
}

private void emit_reward_message_event(object player, mapping applied,
    mapping response_context)
{
    string *lines;
    string text;

    if (!objectp(player) || !mapp(applied))
        return;

    lines = build_reward_gain_lines(applied);
    if (!sizeof(lines))
        return;

    text = implode(lines, "\n") + "\n";
    MESSAGE_D->emit_message_event(player, text, ([
        "channel": "system",
        "intent": "notify",
        "priority": "normal",
        "reliability": "important",
        "display_mode": "paced",
        "response_context": response_context,
        "payload": ([
            "kind": "reward_gain",
            "gold": intp(applied["gold"]) ? applied["gold"] : 0,
            "potential": intp(applied["potential"]) ? applied["potential"] : 0,
            "exp": intp(applied["exp"]) ? applied["exp"] : 0,
            "sect_contribution": intp(applied["sect_contribution"]) ? applied["sect_contribution"] : 0,
            "reputation": mapp(applied["reputation"]) ? copy(applied["reputation"]) : ([ ]),
            "title": stringp(applied["title"]) ? applied["title"] : "",
        ]),
    ]));
}

varargs void sync_reward_state(object player, mapping applied,
    mapping response_context)
{
    int has_inventory_items;
    int has_room_items;
    object room;

    if (!objectp(player) || !mapp(applied))
        return;

    has_inventory_items = arrayp(applied["inventory_items"]) && sizeof(applied["inventory_items"]) > 0;
    has_room_items = arrayp(applied["room_items"]) && sizeof(applied["room_items"]) > 0;

    if (has_room_items) {
        room = environment(player);
        if (has_inventory_items) {
            CLIENT_SYNC_D->request_status_inventory_room(player, room,
                response_context);
            return;
        }

        CLIENT_SYNC_D->request_status_room_snapshot(player, room,
            response_context);
        return;
    }

    if (has_inventory_items) {
        CLIENT_SYNC_D->request_status_inventory(player, response_context);
        return;
    }

    CLIENT_SYNC_D->request_status(player, response_context);
}

varargs void sync_reward_client_state(object player, mapping applied,
    mapping response_context)
{
    if (!objectp(player) || !mapp(applied))
        return;
    /* A missing context is an explicit background/fact notification.  Do not
     * recover the player's latest temp value here: deferred combat/admin and
     * broadcast callers must never inherit an unrelated command. */
    emit_reward_message_event(player, applied, response_context);
    sync_reward_state(player, applied, response_context);
}
