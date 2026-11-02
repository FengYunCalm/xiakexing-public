/**
 * @file    item_inventory_stack.c
 * @brief   物品堆叠、拆分和放入背包模块。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <system/constants.h>

int is_equipped_item(object item);
int can_stack(object item1, object item2);
int consume_test_fault(string stage);

/** 背包内不同物品种类数：非堆叠各算 1 种，可堆叠按 template 分组算 1 种。
 *  与客户端 stackInventoryItemsForDisplay 去重口径对齐；检查与投影共用，单一事实源。 */
int query_stack_room(object dest, object src);
int query_item_stack_amount(object item);
int query_inventory_kind_count(object owner)
{
    object candidate;
    string template_id;
    mapping seen;
    int kind_count;

    if (!objectp(owner))
        return 0;
    seen = ([]);
    kind_count = 0;
    foreach (candidate in all_inventory(owner)) {
        if (candidate->query("stackable")) {
            template_id = candidate->query("template_id") || "";
            if (template_id == "")
                continue;
            if (seen[template_id])
                continue;
            seen[template_id] = 1;
        }
        kind_count++;
    }
    return kind_count;
}

/** 新增物品是否超出种类上限：同 template 堆叠栈已存在时不占新种类。 */
int can_add_item_kind(object owner, object item)
{
    object candidate;
    string template_id;
    int kind_count;

    if (!objectp(owner) || !objectp(item))
        return 1;
    kind_count = query_inventory_kind_count(owner);
    template_id = item->query("template_id") || "";
    if (item->query("stackable") && template_id != "") {
        foreach (candidate in all_inventory(owner)) {
            if (candidate == item)
                continue;
            if ((candidate->query("template_id") || "") != template_id)
                continue;
            /* L-122：同模板栈已经有空间才算“不占新位”。
             * 旧实现只要存在同模板栈就直接放行，满栈再叠加可以无限超容量。 */
            if (query_stack_room(candidate, item) >= query_item_stack_amount(item))
                return 1;
        }
        kind_count++;
    } else {
        kind_count++;
    }
    return kind_count <= INVENTORY_CAPACITY;
}

int query_item_stack_amount(object item)
{
    int amount;

    if (!objectp(item))
        return 0;
    amount = function_exists("query_amount", item) ? item->query_amount() : 0;
    if (!intp(amount) || amount <= 0)
        amount = item->query("amount");
    if (!intp(amount) || amount <= 0)
        amount = item->query("count");
    if (!intp(amount) || amount <= 0)
        amount = 1;
    return amount;
}

void set_item_stack_amount(object item, int amount)
{
    object owner;

    if (!objectp(item))
        return;
    owner = environment(item);
    if (amount <= 0) {
        destruct(item);
        if (objectp(owner)
            && function_exists("query_xk_inventory_generation", owner))
            ITEM_FEEDBACK_D->invalidate_inventory_projection(owner);
        return;
    }
    item->set("amount", amount);
    item->set("count", amount);
    if (objectp(owner)
        && function_exists("query_xk_inventory_generation", owner))
        ITEM_FEEDBACK_D->invalidate_inventory_projection(owner);
}

int query_item_max_stack(object item)
{
    int max_stack;

    if (!objectp(item))
        return 1;

    max_stack = item->query("max_stack");
    if (!intp(max_stack) || max_stack <= 0)
        max_stack = query_item_stack_amount(item);
    return max_stack > 0 ? max_stack : 1;
}

int query_stack_room(object dest, object src)
{
    int max_stack;
    int amount;

    if (!can_stack(dest, src))
        return 0;

    max_stack = query_item_max_stack(dest);
    amount = query_item_stack_amount(dest);
    return max_stack > amount ? max_stack - amount : 0;
}

private int acquisition_policies_match(object left, object right)
{
    if (!objectp(left) || !objectp(right))
        return 0;
    return save_variable(left->query("acquisition/provenance") || ([]))
            == save_variable(right->query("acquisition/provenance") || ([]))
        && (left->query("acquisition/binding_policy") || "")
            == (right->query("acquisition/binding_policy") || "")
        && save_variable(left->query("acquisition/usage_modes") || ({ }))
            == save_variable(right->query("acquisition/usage_modes") || ({ }));
}

int can_stack(object item1, object item2)
{
    string template_id;

    if (!objectp(item1) || !objectp(item2) || item1 == item2)
        return 0;
    if (!item1->query("stackable") || !item2->query("stackable"))
        return 0;
    if (is_equipped_item(item1) || is_equipped_item(item2))
        return 0;
    if (!acquisition_policies_match(item1, item2))
        return 0;

    template_id = item1->query("template_id") || "";
    if (template_id == "" || template_id != (item2->query("template_id") || ""))
        return 0;
    return base_name(item1) == base_name(item2);
}

int merge_items(object dest, object src)
{
    int src_amount;
    int room;

    if (!can_stack(dest, src))
        return 0;

    src_amount = query_item_stack_amount(src);
    room = query_stack_room(dest, src);
    if (src_amount <= 0 || room < src_amount)
        return 0;

    set_item_stack_amount(dest, query_item_stack_amount(dest) + src_amount);
    destruct(src);
    return src_amount;
}

mapping add_item_to_inventory(object owner, object item)
{
    object candidate;
    int amount;

    if (!objectp(owner) || !objectp(item))
        return ([ "ok": 0, "error": "invalid_arguments" ]);
    if (consume_test_fault("inventory_placement"))
        return ([ "ok": 0, "error": "injected_placement_rejected" ]);

    amount = query_item_stack_amount(item);
    if (item->query("stackable")) {
        foreach (candidate in all_inventory(owner)) {
            if (query_stack_room(candidate, item) >= amount
                && merge_items(candidate, item)) {
                ITEM_D->bind_item_owner(candidate, owner, "inventory_merge");
                return ([ "ok": 1, "item": candidate, "merged": 1, "count": amount ]);
            }
        }
    }

    /* 种类上限：非堆叠物品各算 1 种，可堆叠物品按 template 分组算 1 种。
     * 与客户端 stackInventoryItemsForDisplay 的去重口径对齐，避免显示/拒绝边界漂移。 */
    if (!can_add_item_kind(owner, item))
        return ([ "ok": 0, "error": "inventory_capacity_full" ]);

    ITEM_D->ensure_item_identity(item);
    if (!item->move(owner))
        return ([ "ok": 0, "error": "cannot_move" ]);
    ITEM_D->bind_item_owner(item, owner, "inventory");
    if (function_exists("query_xk_inventory_generation", owner))
        ITEM_FEEDBACK_D->invalidate_inventory_projection(owner);
    return ([ "ok": 1, "item": item, "merged": 0, "count": amount ]);
}

object create_item_stack_slice(object item, int count)
{
    mixed raw_provenance;
    mixed raw_usage_modes;
    object slice;
    string binding_policy;
    string owner_id;
    string template_id;

    if (!objectp(item) || count <= 0)
        return 0;
    template_id = item->query("template_id") || "";
    if (template_id == "")
        return 0;
    slice = ITEM_D->create_item(template_id, count);
    if (!objectp(slice))
        return 0;

    raw_provenance = item->query("acquisition/provenance") || ([]);
    slice->set("acquisition/provenance", restore_variable(
        save_variable(raw_provenance)));
    slice->set("provenance", restore_variable(save_variable(
        item->query("provenance") || raw_provenance)));
    binding_policy = item->query("acquisition/binding_policy")
        || item->query("binding_policy") || "none";
    slice->set("acquisition/binding_policy", binding_policy);
    slice->set("binding_policy", binding_policy);
    raw_usage_modes = item->query("acquisition/usage_modes") || ({ });
    slice->set("acquisition/usage_modes", restore_variable(
        save_variable(raw_usage_modes)));
    owner_id = item->query("owner_id") || "";
    slice->set("owner_id", owner_id);
    slice->set("ownership/provenance",
        item->query("ownership/provenance") || "");
    return objectp(slice) ? slice : 0;
}
