/**
 * @file    combat_loot_runtime_d.c
 * @brief   战斗掉落运行时真源,负责掉落奖励数量解析与发放。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

private int resolve_reward_amount(mapping rewards, string fixed_key, string min_key, string range_key)
{
    int fixed_amount;
    int min_amount;
    int range_amount;

    if (!mapp(rewards))
        return 0;

    fixed_amount = rewards[fixed_key];
    if (intp(fixed_amount) && fixed_amount > 0)
        return fixed_amount;

    min_amount = rewards[min_key];
    range_amount = rewards[range_key];
    if (!intp(min_amount) || min_amount <= 0)
        return 0;

    if (intp(range_amount) && range_amount > 0)
        return min_amount + random(range_amount);

    return min_amount;
}

private mapping normalize_loot_item_entry(mixed item_entry, int default_count, int default_chance)
{
    int chance;
    int count;
    string template_id;

    template_id = "";
    count = default_count > 0 ? default_count : 1;
    chance = default_chance >= 0 ? default_chance : 100;

    if (stringp(item_entry)) {
        template_id = item_entry;
    } else if (mapp(item_entry)) {
        template_id = item_entry["template_id"] || item_entry["item"] || item_entry["id"] || "";
        if (intp(item_entry["count"]) && item_entry["count"] > 0)
            count = item_entry["count"];
        if (intp(item_entry["chance"]) && item_entry["chance"] >= 0)
            chance = item_entry["chance"];
    }

    if (!stringp(template_id) || template_id == "")
        return 0;

    return ([
        "template_id": template_id,
        "count": count,
        "chance": chance,
    ]);
}

private mixed *collect_reward_attachments(mapping rewards)
{
    mixed *attachments;
    mixed *candidates;
    mixed *item_list;
    mixed item_entry;
    mapping normalized;
    string item_mode;
    int default_chance;
    int default_count;

    if (!mapp(rewards))
        return ({ });

    attachments = ({ });
    default_count = intp(rewards["item_count"]) && rewards["item_count"] > 0 ? rewards["item_count"] : 1;
    default_chance = intp(rewards["chance"]) && rewards["chance"] >= 0 ? rewards["chance"] : 100;

    normalized = normalize_loot_item_entry(rewards["item"], default_count, default_chance);
    if (mapp(normalized) && random(100) < normalized["chance"]) {
        attachments += ({ ([ "template_id": normalized["template_id"], "count": normalized["count"] ]) });
        return attachments;
    }

    item_list = arrayp(rewards["items"]) ? rewards["items"] : ({ });
    if (sizeof(item_list) == 0)
        return attachments;

    item_mode = rewards["item_mode"];
    if (item_mode == "one") {
        candidates = ({ });
        foreach (item_entry in item_list) {
            normalized = normalize_loot_item_entry(item_entry, default_count, default_chance);
            if (!mapp(normalized) || random(100) >= normalized["chance"])
                continue;
            candidates += ({ normalized });
        }

        if (sizeof(candidates) > 0) {
            normalized = candidates[random(sizeof(candidates))];
            attachments += ({ ([ "template_id": normalized["template_id"], "count": normalized["count"] ]) });
        }

        return attachments;
    }

    foreach (item_entry in item_list) {
        normalized = normalize_loot_item_entry(item_entry, default_count, default_chance);
        if (!mapp(normalized) || random(100) >= normalized["chance"])
            continue;
        attachments += ({ ([ "template_id": normalized["template_id"], "count": normalized["count"] ]) });
    }

    return attachments;
}

mapping settle_npc_defeat_loot(object npc, object killer, mapping rewards, mapping options)
{
    mixed *attachments;
    mapping applied;
    string target_name;
    int exp_reward;
    int coin_reward;
    int drop_on_full;
    string source;
    string target_key;

    if (!objectp(killer) || !userp(killer))
        return ([ "ok": 0, "error": "invalid_killer" ]);
    if (!mapp(rewards))
        return ([ "ok": 0, "error": "invalid_rewards" ]);

    target_name = mapp(options) && stringp(options["target_name"]) && options["target_name"] != ""
        ? options["target_name"]
        : (objectp(npc) ? (npc->query_name() || npc->query("name") || "对手") : "对手");
    exp_reward = resolve_reward_amount(rewards, "exp", "exp_min", "exp_range");
    coin_reward = resolve_reward_amount(rewards, "coin", "coin_min", "coin_range");
    attachments = collect_reward_attachments(rewards);

    drop_on_full = 1;
    if (mapp(options) && intp(options["drop_on_full"]))
        drop_on_full = options["drop_on_full"] ? 1 : 0;
    source = mapp(options) && stringp(options["source"]) && options["source"] != ""
        ? options["source"]
        : "combat_npc_defeat";
    target_key = mapp(options) && stringp(options["target_key"]) && options["target_key"] != ""
        ? options["target_key"]
        : target_name;

    applied = REWARD_APPLY_D->apply_reward_package(killer, ([
        "exp": exp_reward,
        "gold": coin_reward,
        "attachments": attachments,
    ]), ([ "drop_on_full": drop_on_full, "source": source, "target_key": target_key ]));

    if (!mapp(applied) || !applied["ok"])
        return ([
            "ok": 0,
            "error": "apply_failed",
            "target_name": target_name,
            "reward_title": rewards["title"],
            "applied": applied,
        ]);

    REWARD_FEEDBACK_D->sync_reward_client_state(killer, applied);
    return ([
        "ok": 1,
        "target_name": target_name,
        "reward_title": rewards["title"],
        "applied": applied,
        "gain_lines": REWARD_FEEDBACK_D->build_reward_gain_lines(applied),
    ]);
}

/* 按规则挑选物品归属：last_hit=击杀者；total_damage=累计伤害最高者 */
private object resolve_loot_owner(object npc, object killer, string pick_rule)
{
    mapping damage_by;
    string owner_key;
    int best_damage;
    int damage;
    object owner;

    if (pick_rule != "total_damage" || !objectp(npc))
        return killer;

    damage_by = npc->query_temp("combat/damage_by");
    if (!mapp(damage_by) || sizeof(damage_by) == 0)
        return killer;

    best_damage = 0;
    foreach (owner_key, damage in damage_by) {
        if (intp(damage) && damage > best_damage) {
            best_damage = damage;
            owner = find_object(owner_key);
        }
    }
    if (!objectp(owner) || !userp(owner))
        return killer;
    return owner;
}

/* 场景掉落：物品掉在房间地面（可拾取），经验/金钱照常进包 */
private int drop_items_to_scene(object npc, mixed *attachments, object owner)
{
    object room;
    object item;
    mixed attachment;
    string item_id;
    int count;
    int dropped;

    if (!arrayp(attachments) || sizeof(attachments) == 0)
        return 0;
    room = objectp(npc) ? environment(npc) : 0;
    if (!objectp(room) && objectp(owner))
        room = environment(owner);
    if (!objectp(room))
        return 0;

    dropped = 0;
    foreach (attachment in attachments) {
        if (!mapp(attachment))
            continue;
        item_id = attachment["template_id"];
        count = intp(attachment["count"]) && attachment["count"] > 0 ? attachment["count"] : 1;
        item = ITEM_D->create_item_with_affixes(item_id, count);
        if (!objectp(item))
            continue;
        ITEM_D->bind_item_owner(item, owner, "scene_claim");
        if (!item->move(room))
            continue;
        dropped += count;
    }
    if (dropped > 0 && objectp(owner) && userp(owner))
        COMBAT_D->combat_msg(owner, "地面上掉落了你的战利品，记得拾取。\n");
    return dropped;
}

mapping settle_npc_defeat_loot_v2(object npc, object killer, mapping rewards, mapping options)
{
    mixed *attachments;
    string drop_mode;
    string pick_rule;
    object owner;
    object room;
    object item;
    mixed attachment;
    string item_id;
    int count;
    int exp_reward;
    int coin_reward;
    int dropped;
    mapping applied;

    if (!objectp(killer) || !userp(killer))
        return ([ "ok": 0, "error": "invalid_killer" ]);
    if (!mapp(rewards))
        return ([ "ok": 0, "error": "invalid_rewards" ]);

    drop_mode = stringp(rewards["drop_mode"]) && rewards["drop_mode"] != ""
        ? rewards["drop_mode"] : "auto";
    pick_rule = stringp(rewards["pick_rule"]) && rewards["pick_rule"] != ""
        ? rewards["pick_rule"] : "last_hit";

    exp_reward = resolve_reward_amount(rewards, "exp", "exp_min", "exp_range");
    coin_reward = resolve_reward_amount(rewards, "coin", "coin_min", "coin_range");
    attachments = collect_reward_attachments(rewards);

    /* 经验/金钱总是直接进包 */
    applied = REWARD_APPLY_D->apply_reward_package(killer, ([
        "exp": exp_reward,
        "gold": coin_reward,
    ]), ([ "drop_on_full": 1, "source": "combat_npc_defeat", "target_key": rewards["title"] || "对手" ]));
    /* L-133：奖励应用失败必须返回失败状态（与 v1 对齐），不得同步“成功”
     * 客户端状态或继续走掉落；由战斗层给出诚实反馈。 */
    if (!mapp(applied) || !applied["ok"])
        return ([
            "ok": 0,
            "error": "apply_failed",
            "target_name": objectp(npc) ? npc->query_name() : "对手",
            "reward_title": rewards["title"],
            "applied": applied,
        ]);
    REWARD_FEEDBACK_D->sync_reward_client_state(killer, applied);

    owner = resolve_loot_owner(npc, killer, pick_rule);
    if (drop_mode == "scene") {
        dropped = drop_items_to_scene(npc, attachments, owner);
        return ([
            "ok": 1,
            "drop_mode": "scene",
            "pick_rule": pick_rule,
            "owner": objectp(owner) ? owner->query("name") : "对手",
            "dropped": dropped,
            "target_name": objectp(npc) ? npc->query_name() : "对手",
            "reward_title": rewards["title"],
        ]);
    }

    /* auto：物品直接进包，归属按 pick_rule */
    if (sizeof(attachments) > 0) {
        room = objectp(npc) ? environment(npc) : 0;
        foreach (attachment in attachments) {
            item_id = attachment["template_id"];
            count = intp(attachment["count"]) && attachment["count"] > 0 ? attachment["count"] : 1;
            item = ITEM_D->create_item_with_affixes(item_id, count);
            if (objectp(item))
                ITEM_D->bind_item_owner(item, owner, "combat_loot");
            if (objectp(item) && item->move(owner))
                COMBAT_D->combat_msg(owner, "你获得了" + item->query("name") + "。\n");
            else if (objectp(item) && objectp(room) && item->move(room))
                ; /* 已落房间，可 get 拾取 */
        }
    }
    return ([
        "ok": 1,
        "drop_mode": "auto",
        "pick_rule": pick_rule,
        "owner": objectp(owner) ? owner->query("name") : "对手",
        "target_name": objectp(npc) ? npc->query_name() : "对手",
        "reward_title": rewards["title"],
    ]);
}
