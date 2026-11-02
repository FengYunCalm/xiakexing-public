/**
 * @file    money_d.c
 * @brief   货币服务真源



 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <game_constants.h>

inherit "/lib/framework/daemon";

int query_currency_value(object who);
int query_currency_schema_version(object who);
int migrate_currency_schema(object who);
mapping query_currency_denominations();
int add_money(object who, int amount);
private mapping trim_operation_ledger(object who, mapping ledger);
private string _operation_digest(string operation_id);
private int currency_mutation_ready(object who);

/* 被裁剪 id 的墓碑上限：超出后最早墓碑被丢弃（此时保护范围由 watermark 如实暴露）。 */
#define MAX_OPERATION_TOMBSTONES 512

#define CURRENCY_COPPER_ID "item_coin_copper"
#define CURRENCY_SILVER_ID "item_coin_silver"
#define CURRENCY_GOLD_ID   "item_coin_gold"
/* 面额真源在 game_constants.h（100/100/10000）；本地不再重复定义。
 * 历史 v1 面额（1两银 = 1000文）仅用于存量余额迁移换算，不参与新结算。 */
#define CURRENCY_SCHEMA_V2  2
#define LEGACY_COPPER_PER_SILVER 1000
/* 与 item_template.c 货币模板 max_stack 一致；迁移/重建时单 stack 不得超限。 */
#define CURRENCY_MAX_STACK 999999

nosave mapping _last_player_save_phase;

private void invalidate_currency_inventory(object who)
{
    if (objectp(who)
        && function_exists("query_xk_inventory_generation", who))
        ITEM_FEEDBACK_D->invalidate_inventory_projection(who);
}

private mapping persist_player_data_phases(object who)
{
    mixed err;
    mapping phase;

    if (!objectp(who) || !function_exists("save_phases", who))
        return ([ "ok": 0, "persisted": 0, "outcome": "not_written",
            "save_result": 0, "error": "player save phases unavailable" ]);
    err = catch(phase = who->save_phases());
    if (err || !mapp(phase))
        return ([ "ok": 0, "persisted": -1, "outcome": "unknown",
            "save_result": 0, "error": err ? sprintf("%O", err)
                : "save phases unavailable" ]);
    return phase;
}

private int persist_player_data(object who)
{
    _last_player_save_phase = persist_player_data_phases(who);
    return mapp(_last_player_save_phase)
        && _last_player_save_phase["outcome"] == "committed";
}

private mapping recover_player_receipt(object who)
{
    mixed err;
    mapping phase;

    if (!objectp(who) || !function_exists("recover_save_receipt", who))
        return ([ "ok": 1, "persisted": 1, "outcome": "committed",
            "save_result": 1, "recovered": 0 ]);
    err = catch(phase = who->recover_save_receipt());
    if (err || !mapp(phase))
        return ([ "ok": 0, "persisted": -1, "outcome": "unknown",
            "save_result": 0, "error": err ? sprintf("%O", err)
                : "receipt recovery unavailable" ]);
    return phase;
}

private int commit_money_state(object who, mapping changes,
    string *deletes, string operation)
{
    mapping response;
    mapping mutation;

    if (!objectp(who) || !mapp(changes) || !arrayp(deletes))
        return 0;
    mutation = ([ "changes": changes, "deletes": deletes ]);
    response = PLAYER_STATE_D->commit_state_mutations(who, changes, deletes,
        PLAYER_STATE_D->build_state_idempotency_key(who,
            operation || "economy/state", mutation));
    return mapp(response) && response["ok"];
}

private int commit_money_field(object who, string path, mixed value,
    string operation)
{
    return commit_money_state(who, ([ path: value ]), ({ }), operation);
}

private int delete_money_field(object who, string path, string operation)
{
    return commit_money_state(who, ([ ]), ({ path }), operation);
}

private int normalize_amount(mixed value)
{
    if (!intp(value) || value <= 0)
        return 0;

    return value;
}

private int currency_item_value(object item, int silver_value)
{
    string template_id;

    if (!objectp(item))
        return 0;

    template_id = item->query("template_id") || "";
    switch (template_id) {
        case CURRENCY_COPPER_ID:
            return 1;
        case CURRENCY_SILVER_ID:
            return silver_value;
        case CURRENCY_GOLD_ID:
            return COPPER_PER_GOLD;
    }

    return 0;
}

private int query_item_amount(object item)
{
    int amount;

    if (!objectp(item))
        return 0;

    amount = item->query("amount");
    if (!intp(amount) || amount <= 0)
        amount = item->query("count");
    if (!intp(amount) || amount <= 0)
        amount = 1;
    return amount;
}

private void set_item_amount(object item, int amount)
{
    if (!objectp(item))
        return;
    if (amount <= 0) {
        destruct(item);
        return;
    }
    item->set("amount", amount);
    item->set("count", amount);
}

private object find_currency_stack(object who, string item_id)
{
    object item;

    if (!objectp(who) || !stringp(item_id) || item_id == "")
        return 0;

    foreach (item in all_inventory(who)) {
        if (objectp(item) && item->query("template_id") == item_id)
            return item;
    }

    return 0;
}

private mapping collect_currency_stacks(object who)
{
    mapping stacks;
    object item;
    string item_id;

    stacks = ([
        CURRENCY_GOLD_ID: ({}),
        CURRENCY_SILVER_ID: ({}),
        CURRENCY_COPPER_ID: ({}),
    ]);
    if (!objectp(who))
        return stacks;

    foreach (item in all_inventory(who)) {
        if (!objectp(item))
            continue;
        item_id = item->query("template_id") || "";
        if (item_id == CURRENCY_GOLD_ID
            || item_id == CURRENCY_SILVER_ID
            || item_id == CURRENCY_COPPER_ID)
            stacks[item_id] += ({ item });
    }
    return stacks;
}

private mapping currency_target_amounts(int amount)
{
    if (amount < 0)
        amount = 0;

    return ([
        CURRENCY_GOLD_ID: amount / COPPER_PER_GOLD,
        CURRENCY_SILVER_ID: (amount % COPPER_PER_GOLD) / COPPER_PER_SILVER,
        CURRENCY_COPPER_ID: amount % COPPER_PER_SILVER,
    ]);
}

/* 纯函数：按固定面额把总铜文拆成多 stack 计划（每 chunk <= CURRENCY_MAX_STACK）。
 * 禁止把旧银币数量裸乘 10 后塞进单 stack——v1 满 stack 银币换算后必然超限。
 * 返回 mapping：item_id -> ({ chunk, ... })，总铜文精确等于入参。
 * rebalance 的 prepare/commit 均按本计划迭代，分块逻辑只此一份。 */
private mapping build_currency_stack_plan(int total_copper)
{
    mapping plan;
    string item_id;
    int count;
    int remaining;
    int chunk;

    if (total_copper < 0)
        total_copper = 0;

    plan = ([
        CURRENCY_GOLD_ID: ({}),
        CURRENCY_SILVER_ID: ({}),
        CURRENCY_COPPER_ID: ({}),
    ]);
    count = total_copper / COPPER_PER_GOLD;
    if (count > 0)
        plan[CURRENCY_GOLD_ID] = ({ count });
    count = (total_copper % COPPER_PER_GOLD) / COPPER_PER_SILVER;
    if (count > 0)
        plan[CURRENCY_SILVER_ID] = ({ count });
    count = total_copper % COPPER_PER_SILVER;
    if (count > 0)
        plan[CURRENCY_COPPER_ID] = ({ count });

    foreach (item_id in ({ CURRENCY_GOLD_ID, CURRENCY_SILVER_ID,
        CURRENCY_COPPER_ID })) {
        count = sizeof(plan[item_id]) > 0 ? plan[item_id][0] : 0;
        if (count <= CURRENCY_MAX_STACK)
            continue;
        plan[item_id] = ({});
        remaining = count;
        while (remaining > 0) {
            chunk = remaining > CURRENCY_MAX_STACK
                ? CURRENCY_MAX_STACK : remaining;
            plan[item_id] += ({ chunk });
            remaining -= chunk;
        }
    }
    return plan;
}

/* prepare 阶段 undo：merge 进既有 stack 的恢复既有数量，独立新对象 destruct。
 * 逆序回滚保证多次 merge 同一 stack 时逐层还原。 */
private void rollback_currency_prepare(mapping *undo)
{
    mapping entry;
    int index;

    for (index = sizeof(undo) - 1; index >= 0; index--) {
        entry = undo[index];
        if (entry["created"])
            destruct(entry["item"]);
        else
            set_item_amount(entry["item"], entry["before_amount"]);
    }
}

/*
 * A normal debit keeps the canonical currency-item representation but does
 * not churn every existing coin object.  Buy is a hot mutation path: the
 * old reset created and destroyed up to three currency objects on every
 * purchase even when the same stacks could simply receive new counts.
 *
 * All missing stacks are created before any existing balance is changed.
 * Prepare failure returns 0 (created objects already destroyed, merged
 * stacks restored); callers fail closed on the receipt — no destructive
 * rebuild fallback exists.  Targets above CURRENCY_MAX_STACK split into
 * multiple stacks (schema migration path).
 */
private int rebalance_currency_items(object who, int amount)
{
    mapping plan;
    mapping stacks;
    mapping placement;
    mapping *undo;
    object item;
    object candidate;
    object *items;
    string item_id;
    int *chunks;
    int target_total;
    int have;
    int need;
    int chunk;
    int index;
    int before_amount;

    if (!objectp(who) || amount < 0)
        return 0;

    plan = build_currency_stack_plan(amount);
    stacks = collect_currency_stacks(who);
    undo = ({});

    /* prepare：按计划补齐每个币种缺失数量（chunk 序列来自 plan）。
     * 失败只回滚本轮新对象/被 merge 的既有数量，不动其余余额。 */
    foreach (item_id in ({ CURRENCY_GOLD_ID, CURRENCY_SILVER_ID,
        CURRENCY_COPPER_ID })) {
        chunks = plan[item_id];
        items = arrayp(stacks[item_id]) ? stacks[item_id] : ({});
        have = 0;
        foreach (item in items)
            have += query_item_amount(item);
        target_total = 0;
        foreach (chunk in chunks)
            target_total += chunk;
        need = target_total - have;
        /* 既有数据可能含有超过 max_stack 的历史 stack。即使总量无需
         * 增加，也要先补齐计划要求的 stack 数，再由 commit 重分片。 */
        while (sizeof(items) < sizeof(chunks) || need > 0) {
            chunk = need > CURRENCY_MAX_STACK ? CURRENCY_MAX_STACK : need;
            if (chunk <= 0)
                chunk = 1;
            item = ITEM_D->create_item(item_id, chunk);
            if (!objectp(item)) {
                rollback_currency_prepare(undo);
                return 0;
            }
            before_amount = 0;
            foreach (candidate in all_inventory(who)) {
                if (ITEM_INVENTORY_D->query_stack_room(candidate, item)
                    >= query_item_amount(item)) {
                    before_amount = query_item_amount(candidate);
                    break;
                }
            }
            placement = ITEM_INVENTORY_D->add_item_to_inventory(who, item);
            if (!mapp(placement) || !placement["ok"]
                || !objectp(placement["item"])) {
                if (objectp(item))
                    destruct(item);
                rollback_currency_prepare(undo);
                return 0;
            }
            if (placement["item"] == item) {
                undo += ({ ([ "item": item, "before_amount": 0,
                    "created": 1 ]) });
                items += ({ item });
            } else {
                /* 被 inventory merge 进既有 stack：回滚时恢复既有数量。
                 * 不重复登记同一对象，否则 commit 的多余 stack 清理会把
                 * 原 stack 一并销毁。 */
                undo += ({ ([ "item": placement["item"],
                    "before_amount": before_amount,
                    "created": 0 ]) });
                if (member_array(placement["item"], items) == -1)
                    items += ({ placement["item"] });
            }
            need -= chunk;
        }
        stacks[item_id] = items;
    }

    /* commit：按计划 chunk 序列填充（首 stack 满、末 stack 余数），多余
     * stack destruct。prepare 已保证 stack 数 >= chunk 数；set_item_amount/
     * destruct 无失败路径，prepare 成功后提交必然完成。 */
    foreach (item_id in ({ CURRENCY_GOLD_ID, CURRENCY_SILVER_ID,
        CURRENCY_COPPER_ID })) {
        chunks = plan[item_id];
        items = arrayp(stacks[item_id]) ? stacks[item_id] : ({});
        if (sizeof(items) < sizeof(chunks))
            return 0;
        index = 0;
        foreach (chunk in chunks) {
            set_item_amount(items[index], chunk);
            index++;
        }
        for (; index < sizeof(items); index++) {
            if (objectp(items[index]))
                destruct(items[index]);
        }
    }

    return 1;
}

/* 加款统一走 rebalance_currency_items 原子提交原语（prepare 阶段
 * 只创建缺失 stack，失败销毁本次创建对象；全部就绪后 set_item_amount/
 * destruct 无失败路径）。不再逐币种短路——后栈失败不得留下已加部分。 */
private int add_currency_value(object who, int amount)
{
    if (!objectp(who))
        return 0;
    if (amount <= 0)
        return 1;
    return rebalance_currency_items(who, query_currency_value(who) + amount);
}

private int query_currency_value_with(object who, int silver_value)
{
    object item;
    int total;

    if (!objectp(who))
        return 0;

    total = 0;
    foreach (item in all_inventory(who))
        total += currency_item_value(item, silver_value) * query_item_amount(item);
    return total;
}

/* 余额按 schema version 分面额：v1 存量银币按 1000 计值（迁移前），
 * v2 起按 game_constants.h 固定面额 100 计值。 */
private int query_currency_value(object who)
{
    if (query_currency_schema_version(who) >= CURRENCY_SCHEMA_V2)
        return query_currency_value_with(who, COPPER_PER_SILVER);
    return query_currency_value_with(who, LEGACY_COPPER_PER_SILVER);
}

private int query_legacy_money(object who)
{
    int amount;
    mixed value;

    if (!objectp(who))
        return 0;

    value = who->query("money");
    amount = normalize_amount(value);
    if (amount > 0)
        return amount;

    value = who->query("balance");
    return normalize_amount(value);
}

private int clear_legacy_money(object who)
{
    if (!objectp(who))
        return 0;
    return commit_money_state(who, ([ ]),
        ({ "money", "balance" }), "economy/clear_legacy_money");
}

/* 面额 schema 版本：无版本视为 v1（历史 1000 面额），迁移成功后写 v2。 */
int query_currency_schema_version(object who)
{
    mixed value;

    if (!objectp(who))
        return 0;
    value = who->query("economy/currency_schema_version");
    return intp(value) ? value : 1;
}

private mapping *capture_currency_snapshot(object who)
{
    mapping *snapshot;
    object item;
    string item_id;

    snapshot = ({ });
    if (!objectp(who))
        return snapshot;
    foreach (item in all_inventory(who)) {
        if (!objectp(item))
            continue;
        item_id = item->query("template_id") || "";
        if (member_array(item_id, ({ CURRENCY_COPPER_ID,
                CURRENCY_SILVER_ID, CURRENCY_GOLD_ID })) == -1)
            continue;
        snapshot += ({ ([
            "item_id": item_id,
            "amount": query_item_amount(item),
        ]) });
    }
    return snapshot;
}

private int restore_currency_snapshot(object who, mapping *snapshot)
{
    object item;
    object restored;
    object *created;
    mapping entry;
    string item_id;
    int amount;

    if (!objectp(who) || !arrayp(snapshot))
        return 0;

    /* L-103：先验证候选快照并全部建出来，成功后才销毁旧币栈。
     * 原实现先 destruct 再重建，中途失败会把玩家资产直接销毁掉。 */
    created = ({});
    foreach (entry in snapshot) {
        if (!mapp(entry) || !stringp(entry["item_id"])
            || !intp(entry["amount"]) || entry["amount"] <= 0) {
            foreach (item in created) {
                if (objectp(item))
                    destruct(item);
            }
            return 0;
        }
        item_id = entry["item_id"];
        amount = entry["amount"];
        restored = ITEM_D->create_item(item_id, amount);
        if (!objectp(restored) || !restored->move(who)) {
            if (objectp(restored))
                destruct(restored);
            foreach (item in created) {
                if (objectp(item))
                    destruct(item);
            }
            /* 原资产保持不动：宁可恢复失败，也不能把玩家现有币栈销毁。 */
            return 0;
        }
        created += ({ restored });
    }

    foreach (item in all_inventory(who)) {
        if (!objectp(item))
            continue;
        if (member_array(item, created) != -1)
            continue;
        item_id = item->query("template_id") || "";
        if (member_array(item_id, ({ CURRENCY_COPPER_ID,
                CURRENCY_SILVER_ID, CURRENCY_GOLD_ID })) != -1)
            destruct(item);
    }
    return 1;
}

/* 只读预览：把总铜文拆成 stack 计划（不触碰任何对象）。
 * L-102 的可核对入口——用于确认历史超量币栈的补位计划不会逐枚放大。 */
mapping preview_currency_stack_plan(int total_copper)
{
    mapping plan;

    plan = build_currency_stack_plan(total_copper);
    return ([
        "total_copper": total_copper > 0 ? total_copper : 0,
        "max_stack": CURRENCY_MAX_STACK,
        "gold_chunks": plan[CURRENCY_GOLD_ID],
        "silver_chunks": plan[CURRENCY_SILVER_ID],
        "copper_chunks": plan[CURRENCY_COPPER_ID],
    ]);
}

/* 固定面额查询（真源 game_constants.h，供运维/合同核对）。 */
mapping query_currency_denominations()
{
    return ([
        "copper_per_silver": COPPER_PER_SILVER,
        "silver_per_gold": SILVER_PER_GOLD,
        "copper_per_gold": COPPER_PER_GOLD,
    ]);
}

/* v1 -> v2 面额迁移：按旧面额（银=1000）计算总铜文，并把 legacy
 * money/balance 一并纳入同一新 stack；按新面额（银=100）重建多 stack
 * （每 stack <= max_stack，总铜文精确不变）。rebalance 失败恢复旧 stack，
 * 不推进版本；stack、旧 key 清理和版本只在同一次 save 中提交，避免留下
 * v2 stack + legacy key 的可重复合并状态。无币玩家也写 v2。 */
int migrate_currency_schema(object who)
{
    mapping *before_currency;
    int total;
    int legacy_amount;
    int restored;

    if (!objectp(who))
        return 0;
    if (query_currency_schema_version(who) >= CURRENCY_SCHEMA_V2) {
        /* save 失败后内存仍是自洽的 v2 候选；下一个同步点重试 durable
         * save，成功前不清除这个临时 pending 标记。 */
        if (who->query_temp("economy/currency_migration_save_pending")) {
            if (!persist_player_data(who)) {
                log_file("economy", sprintf(
                    "%s currency schema migration retry save failed for %s\n",
                    ctime(time()), get_object_id(who) || "?"));
                return 0;
            }
            who->delete_temp("economy/currency_migration_save_pending");
        }
        return 1;
    }

    before_currency = capture_currency_snapshot(who);
    total = query_currency_value_with(who, LEGACY_COPPER_PER_SILVER);
    legacy_amount = query_legacy_money(who);
    if (legacy_amount > 0)
        total += legacy_amount;
    if (!rebalance_currency_items(who, total))
        return 0;
    if (!commit_money_state(who,
        ([ "economy/currency_schema_version": CURRENCY_SCHEMA_V2 ]),
        ({ "money", "balance" }), "economy/migrate_currency_schema")) {
        restored = restore_currency_snapshot(who, before_currency);
        if (!restored)
            log_file("economy", sprintf(
                "%s currency schema migration memory rollback failed for %s\n",
                ctime(time()), get_object_id(who) || "?"));
        return 0;
    }
    if (!persist_player_data(who)) {
        /* save 使用临时文件+rename；失败时保留自洽的 v2 内存候选并标记
         * retry。下一次 sync 或普通生命周期保存成功前，不能宣称迁移完成。 */
        who->set_temp("economy/currency_migration_save_pending", 1);
        log_file("economy", sprintf("%s currency schema migration save failed for %s\n",
            ctime(time()), get_object_id(who) || "?"));
        return 0;
    }
    invalidate_currency_inventory(who);
    return 1;
}

void sync_player_money(object who)
{
    if (!objectp(who))
        return;

    /* v1 的 stack、legacy key 和 schema 版本统一提交；v2 只保留正式
     * stack 真源。v2 + legacy 只清理旧 key，不再把其值盲目加到正式余额，
     * 防止旧的半迁移快照在重启后重复合并。 */
    if (!migrate_currency_schema(who))
        return;
    if (query_legacy_money(who) > 0) {
        if (!clear_legacy_money(who)) {
            log_file("economy", sprintf("%s legacy currency cleanup state update failed for %s\n",
                ctime(time()), get_object_id(who) || "?"));
            return;
        }
        if (!persist_player_data(who)) {
            /* v2 内存态已经清理 legacy key，但磁盘可能仍保留旧字段；
             * 保留可重试标记，下一次同步先完成同一份候选保存。 */
            who->set_temp("economy/currency_migration_save_pending", 1);
            log_file("economy", sprintf("%s legacy currency cleanup save failed for %s\n",
                ctime(time()), get_object_id(who) || "?"));
        }
    } else {
        clear_legacy_money(who);
    }
}

int query_balance(object who)
{
    if (!objectp(who)) return 0;

    /* L-99：查询是只读入口，不再顺带迁移/落盘。
     * 未迁移账户按旧口径折算余额（query_currency_value 已按 schema 分支），
     * 迁移由登录生命周期与资金变更路径显式触发。 */
    return query_currency_value(who);
}

string format_money(int amount)
{
    int gold;
    int silver;
    int coin;
    string result;

    if (amount < 0)
        amount = 0;

    gold = amount / COPPER_PER_GOLD;
    silver = (amount % COPPER_PER_GOLD) / COPPER_PER_SILVER;
    coin = amount % COPPER_PER_SILVER;

    result = "";
    if (gold > 0)
        result += sprintf("%d两黄金 ", gold);
    if (silver > 0)
        result += sprintf("%d两银 ", silver);
    if (coin > 0 || result == "")
        result += sprintf("%d文铜钱", coin);

    return result;
}

int can_afford(object who, int amount)
{
    return objectp(who) && amount > 0 && query_balance(who) >= amount;
}

int deduct_money(object who, int amount)
{
    int balance;

    if (!objectp(who) || amount <= 0)
        return 0;
    if (!currency_mutation_ready(who))
        return 0;
    balance = query_balance(who);
    if (balance < amount)
        return 0;
    balance -= amount;
    if (rebalance_currency_items(who, balance)) {
        invalidate_currency_inventory(who);
        return 1;
    }
    /* 删除 destructive fallback（清空重建会吞掉失败前的余额状态）；
     * rebalance 失败即返回失败，调用方按 receipt 处理。 */
    return 0;
}

/* 持久幂等扣款。operation_id 记入 player 持久数据（economy/operations），
 * daemon reload/重启后同一 operation 不再重复扣款；ledger 上限 128 条 FIFO。
 * 窗口边界：deduct 与 add 分向独立 ledger（economy/operations 与
 * economy/operations_add 各 128），guild 退款 add-op 不挤掉死亡罚金
 * deduct receipt。 */
int sort_by_value(string a, string b, mapping ledger)
{
    int ta;
    int tb;
    mapping ra;
    mapping rb;

    /* ledger 值为 receipt mapping——按 applied_at（prepared 用
     * prepared_at）排序，裁剪时删最旧（FIFO）。 */
    ra = ledger[a];
    rb = ledger[b];
    ta = mapp(ra) ? (intp(ra["applied_at"]) ? ra["applied_at"] : (intp(ra["prepared_at"]) ? ra["prepared_at"] : 0)) : 0;
    tb = mapp(rb) ? (intp(rb["applied_at"]) ? rb["applied_at"] : (intp(rb["prepared_at"]) ? rb["prepared_at"] : 0)) : 0;
    return ta < tb ? -1 : (ta > tb ? 1 : 0);
}

/* 裁剪 ledger 到 128 条 FIFO；pinned operation（economy/operation_pins）
 * 跳过不裁剪——pending 引用的 receipt 必须保留，防止驱逐后重放多发钱。
 * pins 为单表，不按 ledger 区分裁剪策略。 */
private int money_receipt_matches_spec(mapping receipt, mapping spec)
{
    if (!mapp(receipt) || !mapp(spec))
        return 0;
    if (receipt["amount"] != spec["amount"])
        return 0;
    if (stringp(spec["domain"])
        && spec["domain"] != ""
        && receipt["domain"] != spec["domain"])
        return 0;
    if (stringp(spec["action"])
        && spec["action"] != ""
        && receipt["action"] != spec["action"])
        return 0;
    return 1;
}

private string _operation_digest(string operation_id)
{
    if (!stringp(operation_id) || operation_id == "")
        return "";
    return hash("sha256", operation_id)[0 .. 11];
}

/* 公开判定：该 operation_id 是否已被裁剪（墓碑命中）。
 * true 表示“这条请求早于当前幂等窗口”，调用方必须换新的 operation_id。 */
int is_superseded_operation_id(object who, string operation_id)
{
    mapping tombstones;
    string digest;

    if (!objectp(who))
        return 0;
    digest = _operation_digest(operation_id);
    if (digest == "")
        return 0;
    tombstones = who->query("economy/operations_tombstones");
    return mapp(tombstones) && !undefinedp(tombstones[digest]);
}

private mapping trim_operation_ledger(object who, mapping ledger)
{
    mapping pins;
    mapping tombstones;
    mapping watermark;
    mapping result;
    string digest;
    string oldest_id;
    int oldest_at;
    int stamped_at;
    string *ids;
    string *trim_ids;
    string *removed_ids;
    string id;
    int n;
    int index;

    result = ([
        "ledger": copy(ledger),
        "trimmed_count": 0,
    ]);
    n = sizeof(ledger);
    if (n <= 128)
        return result;
    pins = who->query("economy/operation_pins");
    if (!mapp(pins))
        pins = ([]);
    ids = sort_array(keys(ledger), "sort_by_value", this_object(), ledger);
    trim_ids = ({});
    foreach (id in ids) {
        if (pins[id])
            continue;
        trim_ids += ({ id });
    }
    removed_ids = ({});
    index = 0;
    while (n > 128 && index < sizeof(trim_ids)) {
        id = trim_ids[index];
        map_delete(result["ledger"], id);
        removed_ids += ({ id });
        index++;
        n--;
    }
    if (!sizeof(removed_ids))
        return result;
    /* R52/L-156：裁剪会让被删掉的 operation_id 在之后被当成**新操作**。
     * 这里不做规则猜测（拒绝还是受控恢复需要显式授权），但必须把
     * “幂等保护只覆盖最近 N 条”的事实落盘并可查询，避免把它当成永久保证。 */
    /* 被裁剪掉的 id 用定长摘要留成“墓碑”：窗口外重放宁可拒绝，
     * 也不能被当成一笔新交易再执行一次（摘要冲突只会导致可重试的拒付）。 */
    tombstones = who->query("economy/operations_tombstones");
    tombstones = mapp(tombstones) ? copy(tombstones) : ([ ]);
    foreach (id in removed_ids)
        tombstones[_operation_digest(id)] = time();
    while (sizeof(tombstones) > MAX_OPERATION_TOMBSTONES) {
        oldest_id = "";
        oldest_at = 0;
        foreach (digest, stamped_at in tombstones) {
            if (!intp(stamped_at))
                continue;
            if (oldest_id == "" || stamped_at < oldest_at) {
                oldest_id = digest;
                oldest_at = stamped_at;
            }
        }
        if (oldest_id == "")
            break;
        map_delete(tombstones, oldest_id);
    }
    watermark = who->query("economy/operations_watermark");
    watermark = mapp(watermark) ? copy(watermark) : ([ ]);
    result["tombstones"] = tombstones;
    result["watermark"] = ([
        "trimmed_total": intp(watermark["trimmed_total"])
            ? watermark["trimmed_total"] + sizeof(removed_ids)
            : sizeof(removed_ids),
        "retained": sizeof(result["ledger"]),
        "last_trim_at": time(),
    ]);
    result["trimmed_count"] = sizeof(removed_ids);
    return result;
}

/* 只读暴露裁剪计划，供运维/隔离回归核对 pinned、墓碑与水位；不写玩家。 */
mapping query_operation_ledger_trim_plan(object who, mapping ledger)
{
    if (!objectp(who) || !mapp(ledger))
        return ([ "ok": 0 ]);
    return trim_operation_ledger(who, ledger);
}

/* 幂等窗口水位：retained 是当前保留的 receipt 条数，trimmed_total 是已被裁剪的条数。 */
/* L-95：跨域恢复需要判断“同 operation_id 的资金腿到底有没有发生”。
 * 只读返回账本 receipt 副本（未命中返回 0），不修改账本。 */
mapping query_money_operation_receipt(object who, string operation_id)
{
    mapping ledger;
    mapping receipt;

    if (!objectp(who) || !stringp(operation_id) || operation_id == "")
        return 0;
    ledger = who->query("economy/operation_ledger");
    if (!mapp(ledger))
        return 0;
    receipt = ledger[operation_id];
    return mapp(receipt) ? copy(receipt) : 0;
}

mapping query_operation_ledger_watermark(object who)
{
    mapping watermark;

    if (!objectp(who))
        return ([ "ok": 0 ]);
    watermark = who->query("economy/operations_watermark");
    return ([
        "ok": 1,
        "retained": mapp(watermark) && intp(watermark["retained"]) ? watermark["retained"] : 0,
        "trimmed_total": mapp(watermark) && intp(watermark["trimmed_total"])
            ? watermark["trimmed_total"] : 0,
        "last_trim_at": mapp(watermark) && intp(watermark["last_trim_at"])
            ? watermark["last_trim_at"] : 0,
        "window": 128,
        "tombstones": sizeof(keys(mapp(who->query("economy/operations_tombstones"))
            ? who->query("economy/operations_tombstones") : ([]))),
        "tombstone_window": MAX_OPERATION_TOMBSTONES,
        "window_external_replay_rejected": 1,
        "beyond_tombstone_horizon_guaranteed": 0,
    ]);
}

/* 统一关键资金 operation 提交（无 prepared 窗口）。
 * spec 由 economy_settlement_common::build_money_operation_spec 构造：
 * operation_id/direction/amount/domain/action。
 * 流程（LPC 单线程）：读取旧 snapshot -> 执行余额变更 -> 写 committed
 * receipt -> 同步 player save；save 失败恢复旧余额与 ledger 并返回
 * commit_failed。幂等：同 id committed 返回 duplicate；旧 prepared 残留
 * 返回 unknown 走兼容 reconcile，不盲重试。 */
mapping apply_money_operation(object who, mapping spec)
{
    string operation_id;
    string direction;
    string ledger_key;
    mapping ledger;
    mapping prior;
    mapping legacy;
    mapping receipt;
    mapping ledger_snapshot;
    mapping opposite_ledger;
    mapping trim_result;
    mapping changes;
    mapping tombstones_snapshot;
    mapping watermark_snapshot;
    mapping rollback_changes;
    mapping save_phase;
    string *rollback_deletes;
    string opposite_key;
    int amount;
    int balance_before;
    int saved;
    int had_ledger;
    int had_tombstones;
    int had_watermark;
    int rollback_ok;

    if (!objectp(who) || !mapp(spec))
        return ([ "ok": 0, "state": "invalid_args",
            "last_error": "invalid args" ]);

    operation_id = spec["operation_id"];
    direction = spec["direction"];
    amount = spec["amount"];
    if (!stringp(operation_id) || operation_id == ""
        || (direction != "add" && direction != "deduct")
        || !intp(amount) || amount <= 0)
        return ([ "ok": 0, "state": "invalid_args",
            "operation_id": operation_id, "amount": amount,
            "last_error": "invalid args" ]);

    /* 过旧请求（id 已从账本裁剪）绝不能被当成新操作执行：宁可拒绝并让调用方换新 id。 */
    if (is_superseded_operation_id(who, operation_id))
        return ([ "ok": 0, "applied": 0, "duplicate": 0, "retryable": 1,
            "operation_id": operation_id, "amount": amount,
            "state": "operation_superseded",
            "code": "MONEY_OPERATION_SUPERSEDED",
            "last_error": "operation id predates the idempotency window; retry with a new operation id" ]);

    /* 先处理玩家 JSON 的原收据。业务 operation 重试不能越过上一张
     * 已落盘但未闭合的收据，也不能把未知磁盘状态当作未发生。 */
    save_phase = recover_player_receipt(who);
    if (mapp(save_phase) && save_phase["outcome"] != "committed"
        && save_phase["outcome"] != "not_attempted")
        return ([
            "ok": 0,
            "applied": 0,
            "duplicate": 0,
            "operation_id": operation_id,
            "amount": amount,
            "persisted": save_phase["persisted"],
            "save_result": save_phase["save_result"],
            "pending_recovery": 1,
            "state": save_phase["outcome"],
            "last_error": save_phase["error"] || "player receipt recovery pending",
        ]);

    ledger_key = direction == "add"
        ? "economy/operations_add" : "economy/operations";
    /* 一个 operation_id 在同一账户上不能跨方向复用；否则一个失败的
     * debit 重放可能被另一条 add ledger 当成全新的业务。 */
    opposite_key = direction == "add"
        ? "economy/operations" : "economy/operations_add";
    opposite_ledger = who->query(opposite_key);
    if (mapp(opposite_ledger) && !undefinedp(opposite_ledger[operation_id])) {
        prior = opposite_ledger[operation_id];
        /* L-100：旧版本把 add receipt 也写在 deduct 账本里。只有确认对向账本里
         * 那条 receipt 真的是**反向**记录才算跨方向复用；否则按同向 duplicate 处理，
         * 让历史 guild_refund_* 挂账重放仍然幂等，而不是永远被拒。 */
        if (!mapp(prior)
            || !(prior["direction"] == direction || prior["kind"] == direction)) {
            return ([ "ok": 0, "applied": 0, "duplicate": 0,
                "operation_id": operation_id, "amount": amount,
                "balance": query_balance(who), "state": "param_mismatch",
                "last_error": "operation id reused across money directions" ]);
        }
        if (prior["amount"] != amount) {
            return ([ "ok": 0, "applied": 0, "duplicate": 0,
                "operation_id": operation_id, "amount": amount,
                "balance": query_balance(who), "state": "param_mismatch",
                "last_error": "operation id reused with different amount" ]);
        }
        return ([ "ok": 1, "applied": 0, "duplicate": 1,
            "operation_id": operation_id,
            "amount": prior["amount"] || 0,
            "balance": query_balance(who),
            "persisted": 1, "save_result": 1,
            "pending_recovery": 0,
            "state": "committed",
            "committed_at": prior["committed_at"] || prior["applied_at"] || 0 ]);
    }
    ledger = who->query(ledger_key);
    had_ledger = mapp(ledger);
    ledger_snapshot = had_ledger ? copy(ledger) : ([]);
    tombstones_snapshot = who->query("economy/operations_tombstones");
    had_tombstones = mapp(tombstones_snapshot);
    tombstones_snapshot = had_tombstones ? copy(tombstones_snapshot) : ([]);
    watermark_snapshot = who->query("economy/operations_watermark");
    had_watermark = mapp(watermark_snapshot);
    watermark_snapshot = had_watermark ? copy(watermark_snapshot) : ([]);
    if (mapp(ledger)) {
        prior = ledger[operation_id];
        if (mapp(prior) && prior["state"] == "committed") {
            if (!money_receipt_matches_spec(prior, spec)) {
                return ([ "ok": 0, "applied": 0, "duplicate": 0,
                    "operation_id": operation_id, "amount": amount,
                    "balance": query_balance(who),
                    "state": "param_mismatch",
                    "last_error": "operation id reused with different payload" ]);
            }
            return ([ "ok": 1, "applied": 0, "duplicate": 1,
                "operation_id": operation_id,
                "amount": prior["amount"] || 0,
                "balance": query_balance(who),
                "persisted": 1, "save_result": 1,
                "pending_recovery": 0,
                "state": "committed",
                "committed_at": prior["committed_at"]
                    || prior["applied_at"] || 0 ]);
        }
        if (mapp(prior)) {
            /* 旧 prepared 残留（旧函数写入）：状态不可判定，返回 unknown
             * 由上层按余额核对，不盲重试。 */
            return ([ "ok": 0, "applied": 0, "duplicate": 0,
                "operation_id": operation_id,
                "amount": prior["amount"] || 0,
                "balance": query_balance(who),
                "state": "unknown",
                "last_error": "prepared state ambiguous after reload" ]);
        }
        if (!undefinedp(ledger[operation_id]) && intp(prior)) {
            /* 旧格式条目（时间戳 int）：视为已应用。
             * 缺失 key 在 LPC mapping 中也返回 int 0，不能把新 operation
             * 误判为旧 receipt。 */
            return ([ "ok": 1, "applied": 0, "duplicate": 1,
                "operation_id": operation_id, "amount": 0,
                "balance": query_balance(who),
                "state": "committed", "committed_at": prior ]);
        }
    }

    balance_before = query_balance(who);
    if (direction == "add") {
        if (!add_money(who, amount)) {
            return ([ "ok": 0, "applied": 0, "duplicate": 0,
                "operation_id": operation_id, "amount": amount,
                "balance": query_balance(who), "state": "add_failed",
                "last_error": "add money failed (nothing added)" ]);
        }
    } else {
        if (!deduct_money(who, amount)) {
            return ([ "ok": 0, "applied": 0, "duplicate": 0,
                "operation_id": operation_id, "amount": amount,
                "balance": query_balance(who), "state": "deduct_failed",
                "last_error": "insufficient funds" ]);
        }
    }

    if (!mapp(ledger))
        ledger = ([]);
    receipt = ([ "kind": direction, "direction": direction,
        "operation_id": operation_id,
        "domain": stringp(spec["domain"]) ? spec["domain"] : "",
        "action": stringp(spec["action"]) ? spec["action"] : "",
        "amount": amount, "state": "committed",
        "committed_at": time(), "applied_at": time(),
        "balance_before": balance_before,
        "balance_after": query_balance(who),
        "duplicate": 0,
        "actor": get_object_id(who) ]);
    ledger[operation_id] = receipt;
    trim_result = trim_operation_ledger(who, ledger);
    ledger = trim_result["ledger"];
    changes = ([ ledger_key: ledger ]);
    if (trim_result["trimmed_count"] > 0) {
        changes["economy/operations_tombstones"] = trim_result["tombstones"];
        changes["economy/operations_watermark"] = trim_result["watermark"];
    }
    if (!commit_money_state(who, changes, ({ }),
        "economy/operation_ledger")) {
        rollback_ok = rebalance_currency_items(who, balance_before);
        rollback_changes = ([ ]);
        rollback_deletes = ({ });
        if (had_ledger)
            rollback_changes[ledger_key] = ledger_snapshot;
        else
            rollback_deletes += ({ ledger_key });
        if (had_tombstones)
            rollback_changes["economy/operations_tombstones"] = tombstones_snapshot;
        else
            rollback_deletes += ({ "economy/operations_tombstones" });
        if (had_watermark)
            rollback_changes["economy/operations_watermark"] = watermark_snapshot;
        else
            rollback_deletes += ({ "economy/operations_watermark" });
        rollback_ok = rollback_ok && commit_money_state(who, rollback_changes,
            rollback_deletes, "economy/operation_ledger_rollback");
        return ([ "ok": 0, "applied": 0, "duplicate": 0,
            "operation_id": operation_id, "amount": amount,
            "balance": query_balance(who),
            "state": rollback_ok ? "commit_failed" : "unknown",
            "retryable": rollback_ok ? 0 : 1,
            "pending_recovery": rollback_ok ? 0 : 1,
            "last_error": rollback_ok
                ? "player state commit failed, balance restored"
                : "player state commit failed and rollback is pending" ]);
    }

    /* 同步玩家持久快照。已写待收据和未知状态都保留业务新状态，
     * 只允许明确 not_written/blocked 候选进入旧余额回滚。 */
    saved = persist_player_data(who);
    save_phase = _last_player_save_phase;
    if (mapp(save_phase)
        && save_phase["outcome"] == "persisted_receipt_pending")
        return ([ "ok": 0, "applied": 1, "duplicate": 0,
            "operation_id": operation_id, "amount": amount,
            "balance": query_balance(who), "persisted": 1,
            "save_result": save_phase["save_result"],
            "pending_recovery": 1,
            "state": "persisted_receipt_pending",
            "retry_token": save_phase["retry_token"] || "",
            "last_error": save_phase["error"] || "player receipt pending" ]);
    if (mapp(save_phase) && save_phase["outcome"] == "unknown")
        return ([ "ok": 0, "applied": 1, "duplicate": 0,
            "operation_id": operation_id, "amount": amount,
            "balance": query_balance(who), "persisted": -1,
            "save_result": save_phase["save_result"],
            "pending_recovery": 1, "state": "unknown",
            "retry_token": save_phase["retry_token"] || "",
            "last_error": save_phase["error"] || "player save state unknown" ]);
    if (!saved) {
        rollback_ok = rebalance_currency_items(who, balance_before);
        rollback_changes = ([ ]);
        rollback_deletes = ({ });
        if (had_ledger)
            rollback_changes[ledger_key] = ledger_snapshot;
        else
            rollback_deletes += ({ ledger_key });
        if (had_tombstones)
            rollback_changes["economy/operations_tombstones"] = tombstones_snapshot;
        else
            rollback_deletes += ({ "economy/operations_tombstones" });
        if (had_watermark)
            rollback_changes["economy/operations_watermark"] = watermark_snapshot;
        else
            rollback_deletes += ({ "economy/operations_watermark" });
        rollback_ok = rollback_ok && commit_money_state(who, rollback_changes,
            rollback_deletes, "economy/operation_ledger_rollback");
        return ([ "ok": 0, "applied": 0, "duplicate": 0,
            "operation_id": operation_id, "amount": amount,
            "balance": query_balance(who),
            "state": rollback_ok ? "commit_failed" : "unknown",
            "retryable": rollback_ok ? 0 : 1,
            "pending_recovery": rollback_ok ? 0 : 1,
            "persisted": 0,
            "save_result": save_phase["save_result"],
            "last_error": rollback_ok
                ? "player save failed, balance restored"
                : "player save failed and rollback is pending" ]);
    }
    return ([ "ok": 1, "applied": 1, "duplicate": 0,
        "operation_id": operation_id, "amount": amount,
        "balance": query_balance(who), "persisted": 1,
        "save_result": save_phase["save_result"],
        "pending_recovery": 0, "state": "committed",
        "committed_at": receipt["committed_at"] ]);
}

/* receipt pin——pending 引用的 operation 在 recovery 前 pin，
 * 裁剪跳过 pinned；成功顺序：确认 committed/duplicate -> 内存同时删除
 * pending/unpin -> 一次 save。保存失败恢复 pending 与 pin，避免崩溃窗口
 * 留下不可回收的 orphan pin。 */
int pin_money_operation(object who, string operation_id, string owner_id)
{
    mapping pins;

    if (!objectp(who) || !stringp(operation_id) || operation_id == ""
        || !stringp(owner_id) || owner_id == "")
        return 0;
    pins = who->query("economy/operation_pins");
    if (!mapp(pins))
        pins = ([]);
    if (!undefinedp(pins[operation_id])
        && pins[operation_id] != owner_id)
        return 0;
    pins[operation_id] = owner_id;
    return commit_money_field(who, "economy/operation_pins", pins,
        "economy/pin_operation");
}

int unpin_money_operation(object who, string operation_id, string owner_id)
{
    mapping pins;

    if (!objectp(who) || !stringp(operation_id) || operation_id == "")
        return 0;
    pins = who->query("economy/operation_pins");
    if (!mapp(pins))
        return 1;
    if (pins[operation_id] != owner_id)
        return 0;
    map_delete(pins, operation_id);
    return commit_money_field(who, "economy/operation_pins", pins,
        "economy/unpin_operation");
}

/* 补偿失败进入可恢复队列（economy/compensation_pending）。
 * 同一业务 operation_id 重复排队直接幂等成功；重试始终复用该 ID。 */
int queue_money_compensation(object who, mapping spec)
{
    mapping pending;
    mapping pending_snapshot;
    mapping pins;
    mapping pins_snapshot;
    mapping save_phase;
    string operation_id;
    string pending_key;
    mapping existing;
    int had_pending;
    int had_pins;

    if (!objectp(who) || !mapp(spec))
        return 0;
    operation_id = spec["operation_id"];
    if (!stringp(operation_id) || operation_id == ""
        || !intp(spec["amount"]) || spec["amount"] <= 0
        || (spec["direction"] != "add" && spec["direction"] != "deduct"))
        return 0;
    pending = who->query("economy/compensation_pending");
    had_pending = mapp(pending);
    pending_snapshot = had_pending ? copy(pending) : ([]);
    if (!had_pending)
        pending = ([]);
    pending_key = operation_id;
    if (!undefinedp(pending[pending_key])) {
        existing = pending[pending_key];
        if (mapp(existing)
            && existing["amount"] == spec["amount"]
            && existing["direction"] == spec["direction"]
            && existing["domain"] == spec["domain"]
            && existing["action"] == spec["action"])
            /* 同一挂账的重复排队是幂等成功，必须复用原 operation_id。 */
            return 1;

        /* L-101：同键异参是冲突，不是新业务。旧实现给冲突自动分配 _1/_2 后缀，
         * 于是重试会生成多笔补偿；这里改为 fail-closed，调用方要用显式新 ID 走新操作。 */
        log_file("economy", sprintf(
            "%s compensation queue conflict: operation=%s existing=%O incoming=%O\n",
            ctime(time()), operation_id, existing, spec));
        return 0;
    }
    operation_id = pending_key;

    pins = who->query("economy/operation_pins");
    had_pins = mapp(pins);
    pins_snapshot = had_pins ? copy(pins) : ([]);
    if (mapp(pins) && pins[pending_key]
        && pins[pending_key] != "MONEY_D")
        return 0;
    if (!pin_money_operation(who, pending_key, "MONEY_D"))
        return 0;
    pending[pending_key] = ([
        "operation_id": pending_key,
        "amount": spec["amount"],
        "direction": spec["direction"],
        "domain": spec["domain"],
        "action": spec["action"],
        "created_at": time(),
        "attempts": 0,
        "state": "pending",
    ]);
    if (!commit_money_field(who, "economy/compensation_pending", pending,
        "economy/queue_compensation")) {
        if (had_pins)
            commit_money_field(who, "economy/operation_pins", pins_snapshot,
                "economy/queue_compensation_rollback");
        else
            delete_money_field(who, "economy/operation_pins",
                "economy/queue_compensation_rollback");
        return 0;
    }
    save_phase = persist_player_data_phases(who);
    if (mapp(save_phase)
        && (save_phase["outcome"] == "persisted_receipt_pending"
            || save_phase["outcome"] == "unknown")) {
        /* The queue and its pin are already part of the accepted candidate.
         * Keep both in memory; the normal player receipt owner will finish
         * this exact save and no new compensation identity is created. */
        log_file("economy", sprintf(
            "%s compensation queue accepted with pending player receipt for %s\n",
            ctime(time()), get_object_id(who) || "?"));
        return 1;
    }
    if (!mapp(save_phase) || save_phase["outcome"] != "committed") {
        if (mapp(save_phase) && save_phase["outcome"] != "not_written")
            return 0;
        if (had_pending)
            commit_money_field(who, "economy/compensation_pending",
                pending_snapshot, "economy/queue_compensation_rollback");
        else
            delete_money_field(who, "economy/compensation_pending",
                "economy/queue_compensation_rollback");
        if (had_pins)
            commit_money_field(who, "economy/operation_pins", pins_snapshot,
                "economy/queue_compensation_rollback");
        else
            delete_money_field(who, "economy/operation_pins",
                "economy/queue_compensation_rollback");
        log_file("economy", sprintf("%s compensation queue save failed for %s\n",
            ctime(time()), get_object_id(who) || "?"));
        return 0;
    }
    return 1;
}

/* 恢复补偿队列——重试所有 pending 补偿（幂等 operation id），
 * 成功删除，失败 attempts++ 保留。返回结构化状态，供生命周期健康快照
 * 区分完全恢复和仍有待处理条目。 */
mapping recover_pending_compensations(object who)
{
    mapping pending;
    mapping entry;
    mapping receipt;
    string *keys;
    string key;
    int recovered;
    int all_ok;
    int saved;
    mapping save_phase;

    if (!objectp(who))
        return ([ "ok": 0, "recovered": 0, "pending": 0,
            "error": "invalid player" ]);
    pending = who->query("economy/compensation_pending");
    if (!mapp(pending) || sizeof(pending) == 0)
        return ([ "ok": 1, "recovered": 0, "pending": 0 ]);
    all_ok = 1;
    keys = keys(pending);
    foreach (key in keys) {
        entry = pending[key];
        if (!mapp(entry) || !intp(entry["amount"]) || entry["amount"] <= 0) {
            all_ok = 0;
            continue;
        }
        /* 兼容 pin 功能上线前已存在的挂账；先补 pin，再执行幂等操作，
         * 避免操作已提交后因缺 pin 永久滞留。 */
        if (!stringp(entry["operation_id"])
            || entry["operation_id"] == ""
            || !pin_money_operation(who, entry["operation_id"], "MONEY_D")) {
            entry["attempts"] = (entry["attempts"] || 0) + 1;
            entry["state"] = "retry_failed";
            entry["last_error"] = "receipt pin owner conflict";
            pending[key] = entry;
            commit_money_field(who, "economy/compensation_pending", pending,
                "economy/recover_compensation_retry");
            all_ok = 0;
            save_phase = persist_player_data_phases(who);
            if (!mapp(save_phase)
                || save_phase["outcome"] != "committed")
                log_file("economy", sprintf(
                    "%s compensation pin retry save is not committed for %s: %O\n",
                    ctime(time()), get_object_id(who) || "?", save_phase));
            continue;
        }
        receipt = apply_money_operation(who, ([
            "operation_id": entry["operation_id"],
            "direction": entry["direction"],
            "amount": entry["amount"],
            "domain": entry["domain"],
            "action": entry["action"],
        ]));
        if (mapp(receipt) && receipt["ok"]) {
            map_delete(pending, key);
            if (!commit_money_field(who, "economy/compensation_pending", pending,
                "economy/recover_compensation_complete")) {
                pending[key] = entry;
                all_ok = 0;
                continue;
            }
            if (!unpin_money_operation(who, entry["operation_id"], "MONEY_D")) {
                pending[key] = entry;
                commit_money_field(who, "economy/compensation_pending", pending,
                    "economy/recover_compensation_rollback");
                all_ok = 0;
                continue;
            }
            saved = persist_player_data(who);
            save_phase = _last_player_save_phase;
            if (saved) {
                recovered++;
                continue;
            }
            if (mapp(save_phase) && save_phase["outcome"] == "not_written") {
                /* The money leg was already committed by apply_money_operation;
                 * only the pending-removal candidate was rejected. Restore the
                 * queue and pin in memory so the next login matches disk. */
                pending[key] = entry;
                commit_money_field(who, "economy/compensation_pending", pending,
                    "economy/recover_compensation_rollback");
                pin_money_operation(who, entry["operation_id"], "MONEY_D");
            }
            /* persisted_receipt_pending/unknown keep the removal candidate:
             * the publication file may already contain the cleared queue, so
             * rolling it back would risk replaying a committed compensation. */
            log_file("economy", sprintf(
                "%s compensation recovery save failed or is not committed for %s: %O\n",
                ctime(time()), get_object_id(who) || "?", save_phase));
            all_ok = 0;
            continue;
        } else {
            entry["attempts"] = (entry["attempts"] || 0) + 1;
            entry["state"] = "retry_failed";
            entry["last_error"] = mapp(receipt) && receipt["last_error"]
                ? receipt["last_error"] : "compensation failed";
            pending[key] = entry;
            commit_money_field(who, "economy/compensation_pending", pending,
                "economy/recover_compensation_retry");
            all_ok = 0;
            save_phase = persist_player_data_phases(who);
            if (!mapp(save_phase) || save_phase["outcome"] != "committed")
                log_file("economy", sprintf(
                    "%s compensation retry state save failed or is not committed for %s: %O\n",
                    ctime(time()), get_object_id(who) || "?", save_phase));
        }
    }
    return ([ "ok": all_ok, "recovered": recovered,
        "pending": mapp(pending) ? sizeof(pending) : 0 ]);
}


/* L-99：资金变更前必须确认货币迁移已经落盘。
 * 迁移失败留下的是“v2 内存态 + v1 存档”的半迁移状态；此时继续变更资金，
 * 会在下一次加载时把两套口径叠加。这里先重试落盘，仍失败就拒绝本次变更。 */
private int currency_mutation_ready(object who)
{
    if (!objectp(who))
        return 0;
    if (who->query_temp("economy/currency_migration_save_pending")
        && !migrate_currency_schema(who))
        return 0;
    return migrate_currency_schema(who);
}

int add_money(object who, int amount)
{
    int added;

    if (!objectp(who) || amount <= 0) return 0;
    if (!currency_mutation_ready(who))
        return 0;
    added = add_currency_value(who, amount);
    invalidate_currency_inventory(who);
    return added;
}

nosave int _transfer_operation_sequence;
nosave string _transfer_operation_epoch;

private string issue_transfer_operation_id(object from, object to)
{
    if (!_transfer_operation_epoch)
        _transfer_operation_epoch = sprintf("%016x%016x%016x",
            secure_random(MAX_INT), secure_random(MAX_INT), secure_random(MAX_INT));
    _transfer_operation_sequence++;
    return sprintf("money/transfer/%s/%s/%s/%d", get_object_id(from) || "from",
        get_object_id(to) || "to", _transfer_operation_epoch, _transfer_operation_sequence);
}

private int is_player_money_owner(object who)
{
    return objectp(who) && function_exists("is_player", who)
        && call_other(who, "is_player");
}

varargs int transfer_money_with_operation(object from, object to, int amount,
    string operation_id)
{
    mapping receipt;

    if (!objectp(from) || !objectp(to) || amount <= 0)
        return 0;
    if (!stringp(operation_id) || operation_id == "")
        operation_id = issue_transfer_operation_id(from, to);

    if (is_player_money_owner(from)) {
        receipt = apply_money_operation(from, ([
            "operation_id": operation_id + "/debit",
            "direction": "deduct",
            "amount": amount,
            "domain": "transfer",
            "action": "debit",
        ]));
        if (!mapp(receipt) || receipt["ok"] != 1)
            return 0;
    } else if (!deduct_money(from, amount)) {
        return 0;
    }

    if (is_player_money_owner(to)) {
        receipt = apply_money_operation(to, ([
            "operation_id": operation_id + "/credit",
            "direction": "add",
            "amount": amount,
            "domain": "transfer",
            "action": "credit",
        ]));
    } else {
        receipt = ([ "ok": add_money(to, amount) ? 1 : 0 ]);
    }
    if (mapp(receipt) && receipt["ok"] == 1)
        return 1;

    /* 接收方失败时按同一 transfer 身份做幂等退款；失败则挂账，不能
     * 继续把一次性时间戳拼接成新 operation 掩盖未完成补偿。 */
    if (is_player_money_owner(from)) {
        receipt = apply_money_operation(from, ([
            "operation_id": operation_id + "/refund",
            "direction": "add",
            "amount": amount,
            "domain": "transfer",
            "action": "rollback_refund",
        ]));
        if (!mapp(receipt) || receipt["ok"] != 1)
            queue_money_compensation(from, ([
                "operation_id": operation_id + "/refund",
                "direction": "add",
                "amount": amount,
                "domain": "transfer",
                "action": "rollback_refund",
            ]));
    } else {
        add_money(from, amount);
    }
    return 0;
}

int transfer_money(object from, object to, int amount)
{
    return transfer_money_with_operation(from, to, amount, 0);
}

int parse_currency_amount(string text)
{
    int amount;
    string unit;

    if (!stringp(text) || text == "")
        return 0;

    text = replace_string(text, " ", "");
    if (sscanf(text, "%d%s", amount, unit) != 2 || amount <= 0)
        return 0;

    switch (unit) {
        case "文":
        case "铜钱":
        case "文铜钱":
        case "铜板":
        case "copper":
        case "coin":
            return amount;
        case "银两":
        case "两银":
        case "两银两":
        case "银子":
        case "两银子":
        case "silver":
            return amount * COPPER_PER_SILVER;
        case "黄金":
        case "两金":
        case "两黄金":
        case "金子":
        case "gold":
            return amount * COPPER_PER_GOLD;
    }

    return 0;
}

int is_currency_reference(string text)
{
    if (!stringp(text) || text == "")
        return 0;
    if (parse_currency_amount(text) > 0)
        return 1;
    return text == CURRENCY_COPPER_ID
        || text == CURRENCY_SILVER_ID
        || text == CURRENCY_GOLD_ID
        || text == "铜钱"
        || text == "铜板"
        || text == "银两"
        || text == "银子"
        || text == "黄金"
        || text == "金子"
        || text == "copper"
        || text == "coin"
        || text == "silver"
        || text == "gold";
}

void create()
{
    ::create();
    /* 补偿恢复经 player_lifecycle manifest 装配，本 daemon 不再
     * create 时注册。 */
}
