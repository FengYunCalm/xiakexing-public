#pragma strict_types

#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

/**
 * @file    claimweapon.c
 * @brief   领兵器命令：六扇门弟子在兵器房按武学路线领取一件训练武器（每角色一次）。
 *
 * 可领：铁尺（staff）、练习刀（blade）、白蜡杆（spear）、木剑（sword）、短棍（staff）。
 * 默认领铁尺（六扇门标配）。
 */

#define CLAIM_WEAPON_ITEMS ({ "item_tiechi", "item_blade_practice", "item_spear_practice", "item_sword_wood", "item_staff_short" })

int help(object me);

private int is_claimable_weapon(string item_id)
{
    return member_array(item_id, CLAIM_WEAPON_ITEMS) != -1;
}

int main(object me, string arg)
{
    string item_id;
    object item;

    if (!objectp(me))
        return 0;

    if ((me->query("family/id") || "") != "liushanmen") {
        msg_send(me, "公家的兵器，不是公家的人不能领。\n");
        return 1;
    }
    if (me->query("sect/weapon_claimed")) {
        msg_send(me, "你已经领过兵器了。\n");
        return 1;
    }

    item_id = "item_tiechi";
    if (stringp(arg) && arg != "") {
        item_id = trim(arg);
        if (!is_claimable_weapon(item_id)) {
            msg_send(me, "兵器架上没有这件兵器。\n");
            return 1;
        }
    }
    if (present(item_id, me)) {
        msg_send(me, "你身上已经有这件兵器了。\n");
        return 1;
    }

    item = ITEM_D->create_item(item_id, 1);
    if (!objectp(item) || !item->move(me)) {
        msg_send(me, "兵器架上空了，改日再来。\n");
        return 1;
    }

    me->set("sect/weapon_claimed", 1);
    me->schedule_save();
    msg_send(me, sprintf("你从兵器架上取下%s，掂了掂，正合手。\n"
        "「兵器是拿人的家伙，不是摆设。」教头的话在耳朵里转了一圈。\n",
        item->query("name") || "兵器"));
    ROOM_VIEW_D->invalidate_room_view_content(environment(me));
    return 1;
}

int help(object me)
{
    msg_send(me, "命令：claimweapon [兵器ID]\n"
        "在六扇门兵器房按武学路线领取一件训练武器（每名弟子限一次）。\n"
        "可领：item_tiechi（铁尺）/ item_blade_practice（练习刀）/ "
        "item_spear_practice（白蜡杆）/ item_sword_wood（木剑）/ item_staff_short（短棍）。\n");
    return 1;
}
