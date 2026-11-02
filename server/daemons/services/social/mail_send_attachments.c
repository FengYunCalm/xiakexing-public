/**
 * @file    mail_send_attachments.c
 * @brief   邮件物品附件辅助子服务



 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

mapping build_item_attachment(object me, string item_name, int count)
{
    object item;
    mapping canonical;
    int amount;

    if (!objectp(me) || !stringp(item_name) || item_name == "" || count <= 0)
        return 0;

    /* L-126：附件记录的构建规则收敛到物品 owner（ITEM_INVENTORY_D），
     * 不在邮件侧复制第二份实现——旧实现漏了 no_drop 且与其它调用方漂移。
     * 邮件侧只保留自己的语义：按名字定位、显式数量切分。 */
    item = ITEM_INVENTORY_D->query_transferable_item(me, item_name);
    if (!objectp(item))
        return 0;
    canonical = ITEM_INVENTORY_D->build_item_attachment_from_object(item);
    if (!mapp(canonical))
        return 0;

    amount = ITEM_INVENTORY_D->query_item_stack_amount(item);
    if (amount <= 0)
        amount = 1;

    if (item->query("stackable")) {
        if (count > amount)
            return 0;
    } else if (count != 1) {
        return 0;
    }

    return ([
        "template_id": canonical["template_id"],
        "lookup_id": canonical["lookup_id"],
        "item_path": canonical["item_path"],
        "count": count,
        "item_name": get_object_name(item) || canonical["template_id"],
    ]);
}

int consume_item_attachment(object me, mapping attachment)
{
    object item;
    int remain;
    int amount;
    string item_name;
    string lookup_id;

    if (!objectp(me) || !mapp(attachment))
        return 0;

    item_name = attachment["item_name"];
    lookup_id = attachment["lookup_id"];
    item = call_other("/lib/std/utils/util_item", "find_item_in_collection",
        all_inventory(me), item_name);
    if (!objectp(item))
        item = present(lookup_id, me);
    if (!objectp(item))
        item = present(attachment["template_id"], me);
    if (!objectp(item))
        return 0;

    /* L-126：栈数量与写回统一走物品 owner，避免邮件侧自建第二套语义。 */
    amount = ITEM_INVENTORY_D->query_item_stack_amount(item);
    if (amount <= 0)
        amount = 1;

    if (item->query("stackable")) {
        remain = amount - (attachment["count"] || 1);
        if (remain < 0)
            return 0;
        if (remain == 0) {
            destruct(item);
            return 1;
        }
        ITEM_INVENTORY_D->set_item_stack_amount(item, remain);
        return 1;
    }

    destruct(item);
    return 1;
}
