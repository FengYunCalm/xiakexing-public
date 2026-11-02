/**
 * @file    player_persistence.c
 * @brief   玩家持久化子模块



 *
 * @par Description:
 * 基础对象层子模块，供上层对象组合能力。
 */

#pragma strict_types

#include <system/services.h>

string query_name();

mixed query(string path);
int set(string path, mixed value);

/* 嵌套容器树深度上限：够用且防止异常数据递归过深。 */
#define MAX_INVENTORY_SNAPSHOT_DEPTH 5

/* L-23：快照按树写入。contents 只在容器确实装着物品时出现，
 * 旧档（没有该字段）与以前的行为完全一致，不需要迁移。 */
private mapping build_item_entry(object item, int depth)
{
    mapping entry;
    mapping item_data;
    mapping *contents;
    object *inv;
    object inner;

    item_data = function_exists("query_data", item) ? item->query_data() : 0;
    if (!mapp(item_data))
        item_data = ([]);
    entry = ([
        "path": base_name(item),
        "data": item_data,
        "equipped": function_exists("query_equipped", item) && item->query_equipped()
    ]);
    if (depth >= MAX_INVENTORY_SNAPSHOT_DEPTH)
        return entry;

    inv = all_inventory(item);
    contents = ({ });
    foreach (inner in inv) {
        if (!objectp(inner) || interactive(inner))
            continue;
        contents += ({ build_item_entry(inner, depth + 1) });
    }
    if (sizeof(contents) > 0)
        entry["contents"] = contents;
    return entry;
}

mapping *build_inventory_snapshot(object player)
{
    object *inv;
    object item;
    mapping *snapshot;

    if (!objectp(player))
        return ({ });

    inv = all_inventory(player);
    snapshot = ({ });
    foreach (item in inv) {
        if (!objectp(item) || interactive(item))
            continue;
        snapshot += ({ build_item_entry(item, 0) });
    }

    return snapshot;
}

void prepare_player_persistence_snapshot(object player)
{
    if (!objectp(player))
        return;

    player->set("persistence/inventory", build_inventory_snapshot(player));
}

/* 已放置但内容未补齐的容器：按 path + template_id 唯一匹配，避免重复建对象。 */
private object find_partially_restored_container(object player, mapping entry)
{
    object candidate;
    object found;
    string path;
    string want_template;
    string have_template;
    mapping data;

    path = entry["path"];
    data = entry["data"];
    want_template = mapp(data) && stringp(data["template_id"]) ? data["template_id"] : "";
    found = 0;
    foreach (candidate in all_inventory(player)) {
        if (!objectp(candidate) || base_name(candidate) != path)
            continue;
        have_template = stringp(candidate->query("template_id"))
            ? candidate->query("template_id") : "";
        if (want_template != "" && have_template != want_template)
            continue;
        /* 多个候选说明无法判定归属，宁可新建也不并错对象。 */
        if (objectp(found))
            return 0;
        found = candidate;
    }
    return found;
}

/* 把 entry["contents"] 递归放进 container；返回失败条目供上层保留 pending。 */
private mapping restore_item_contents(object container, mapping entry, int depth)
{
    mapping result;
    mapping *contents;
    mapping inner_entry;
    mapping inner_result;
    mapping placement;
    object inner;
    string path;
    mixed err;

    result = ([ "ok": 1, "failed": 0, "pending": ({ }) ]);
    if (depth >= MAX_INVENTORY_SNAPSHOT_DEPTH)
        return result;
    contents = entry["contents"];
    if (!arrayp(contents))
        return result;

    foreach (inner_entry in contents) {
        if (!mapp(inner_entry) || !stringp(inner_entry["path"])
            || inner_entry["path"] == "") {
            result["ok"] = 0;
            result["failed"] += 1;
            result["pending"] += ({ inner_entry });
            continue;
        }
        path = inner_entry["path"];
        inner = 0;
        err = catch(inner = new(path));
        if (err || !objectp(inner)) {
            result["ok"] = 0;
            result["failed"] += 1;
            result["pending"] += ({ inner_entry });
            continue;
        }
        if (mapp(inner_entry["data"]) && function_exists("set_data", inner))
            inner->set_data(inner_entry["data"]);
        if (function_exists("set", inner))
            inner->set("equipped", 0);

        placement = ITEM_INVENTORY_D->add_item_to_inventory(container, inner);
        if (!mapp(placement) || !placement["ok"]) {
            if (objectp(inner) && !environment(inner))
                destruct(inner);
            result["ok"] = 0;
            result["failed"] += 1;
            result["pending"] += ({ inner_entry });
            continue;
        }
        inner_result = restore_item_contents(placement["item"], inner_entry, depth + 1);
        if (!inner_result["ok"]) {
            result["ok"] = 0;
            result["failed"] += inner_result["failed"];
            result["pending"] += inner_result["pending"];
        }
    }
    return result;
}

mapping restore_player_inventory_snapshot(object player)
{
    mixed snapshot;
    mapping entry;
    mapping placement;
    mapping result;
    mapping changes;
    mapping response;
    mapping *failures;
    mapping *pending;
    mapping pending_entry;
    mapping content_result;
    object item;
    string path;
    string error;
    mapping item_data;
    mixed err;

    result = ([
        "ok": 1,
        "restored_count": 0,
        "failed_count": 0,
        "failures": ({ }),
        "pending_snapshot": 0,
    ]);

    if (!objectp(player))
        return ([ "ok": 0, "restored_count": 0, "failed_count": 0,
            "failures": ({ }), "pending_snapshot": 0,
            "error": "player_required" ]);

    snapshot = player->query("persistence/inventory");
    if (!arrayp(snapshot)) {
        player->set("persistence/inventory_restore_pending", 0);
        return result;
    }

    /* 快照条目按原样交给物品服务放置：堆叠、绑定策略与容量规则由
     * ITEM_INVENTORY_D 作为唯一 owner 决定，玩家基类不再维护第二套
     * 合并规则；失败条目保留原始形态等待重试。 */
    failures = ({ });
    pending = ({ });

    foreach (entry in snapshot) {
        if (!mapp(entry)) {
            failures += ({ ([ "path": "", "error": "snapshot_entry_invalid" ]) });
            continue;
        }

        path = entry["path"];
        if (!stringp(path) || path == "") {
            failures += ({ ([ "path": "", "error": "snapshot_path_invalid" ]) });
            pending += ({ entry });
            continue;
        }

        item = 0;
        /* 上一轮已放置外层物件但内容未补齐：复用原对象，只补缺失内容。 */
        if (entry["partial_restore"])
            item = find_partially_restored_container(player, entry);
        if (!objectp(item)) {
            err = catch(item = new(path));
            if (err || !objectp(item)) {
                failures += ({ ([ "path": path, "error": err ? sprintf("%O", err)
                    : "item_load_failed" ]) });
                pending += ({ entry });
                continue;
            }
        }

        item_data = entry["data"];
        if (mapp(item_data) && function_exists("set_data", item))
            item->set_data(item_data);

        /* `equipped` is persisted for presentation only. Rebuild the actual
         * runtime slot below, so a failed restore cannot leave a stale UI flag. */
        if (function_exists("set", item))
            item->set("equipped", 0);

        placement = ITEM_INVENTORY_D->add_item_to_inventory(player, item);
        if (!mapp(placement) || !placement["ok"]) {
            error = mapp(placement) && stringp(placement["error"])
                ? placement["error"] : "placement_rejected";
            if (objectp(item) && !environment(item))
                destruct(item);
            failures += ({ ([ "path": path, "error": error ]) });
            pending += ({ entry });
            continue;
        }
        item = placement["item"];
        result["restored_count"] += 1;

        /* L-23：容器内容按树恢复；内容有缺失时外层保留 pending（带 partial_restore），
         * 下一轮只补内容，不会重复创建外层物件。 */
        if (arrayp(entry["contents"])) {
            content_result = restore_item_contents(item, entry, 1);
            if (!content_result["ok"]) {
                failures += ({ ([ "path": path,
                    "error": "container_contents_incomplete",
                    "failed": content_result["failed"] ]) });
                pending_entry = entry;
                pending_entry["partial_restore"] = 1;
                pending_entry["contents"] = content_result["pending"];
                pending += ({ pending_entry });
                continue;
            }
        }

        if (!entry["equipped"])
            continue;

        if (function_exists("query_item_type", item) && item->query_item_type() == "weapon") {
            if (function_exists("wield", item)
                && !objectp(player->query_temp("weapon"))
                && item->wield(player))
                continue;
            if (function_exists("set", item))
                item->set("equipped", 0);
            continue;
        }

        if (function_exists("query_item_type", item) && item->query_item_type() == "armor") {
            if (function_exists("wear", item) && item->wear(player))
                continue;
            if (function_exists("set", item))
                item->set("equipped", 0);
            continue;
        }

        if (function_exists("equip", item) && item->equip(player))
            continue;
        if (function_exists("set", item))
            item->set("equipped", 0);
    }

    if (sizeof(failures) > 0) {
        result["ok"] = 0;
        result["failed_count"] = sizeof(failures);
        result["failures"] = failures;
        result["pending_snapshot"] = 1;
        /* 只保留失败条目：已放置条目不回写，重复恢复不会复制物品；
         * pending 未清除前 save() 不得用部分运行态覆写这份存档。 */
        changes = ([
            "persistence/inventory": pending,
            "persistence/inventory_restore_pending": 1,
        ]);
    } else {
        /* 条目已全部消费，清空快照避免重复恢复；下一次 save() 从运行态重建。 */
        changes = ([
            "persistence/inventory": ({ }),
            "persistence/inventory_restore_pending": 0,
        ]);
    }
    /* 两个 key 同一批次提交：pending 快照与 pending 标志不允许半提交。
     * 提交失败必须暴露（否则失败条目在下一次 save 时被静默丢弃）。 */
    response = PLAYER_STATE_D->commit_state_fields(player, changes,
        PLAYER_STATE_D->build_state_idempotency_key(player,
            "persistence/inventory_restore", changes));
    if (!mapp(response) || !response["ok"]) {
        result["ok"] = 0;
        result["state_commit_failed"] = 1;
        result["pending_snapshot"] = sizeof(failures) > 0 ? 1 : 0;
    }

    // 装备恢复走物件层直穿（wear/wield 直 set_temp，不经服务层出口），
    // 循环毕补一次咽喉点同步——投影失效所有权归调用方（服务层同先例）。
    // 已知搭售：sync 内含 inventory 投影失效＋状态刷新包，与登录初始全量
    // 同步重叠；登录窗一次性成本，幂等无害，不为此另设纯失效变体。
    ITEM_FEEDBACK_D->sync_equipment_change(player);
    return result;
}
