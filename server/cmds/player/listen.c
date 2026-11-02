#pragma strict_types

#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

int help(object me);

/*
 * 偷听（按钮 cmd：listen <spot_id>，玩家不手输）。
 *
 * 偷听点由 listen_points.c 注册（房间 → 偷听点列表），偷听对象壳
 * （npc_yz_listen_spot）提供弹窗入口。成功听到线索记入玩家
 * listen/clues（去重），是当铺脏货/旧渡的灯等支线的触发前提；
 * 失败被察觉，短文案提示，不惩罚。
 */
private mapping query_spot_in_room(object me, string spot_id)
{
    mapping points;
    mapping spot;
    mapping *spots;
    int i;

    points = CONTENT_REGISTRY_D->query_listen_points();
    if (!mapp(points) || sizeof(keys(points)) == 0)
        return 0;
    spots = points[base_name(environment(me))];
    if (!arrayp(spots))
        return 0;
    if (stringp(spot_id) && spot_id != "") {
        for (i = 0; i < sizeof(spots); i++) {
            if (spots[i]["id"] == spot_id)
                return spots[i];
        }
        return 0;
    }
    /* 未指定偷听点：房间只有一个时直接偷听 */
    if (sizeof(spots) == 1)
        return spots[0];
    return 0;
}

private int record_clue(object me, string clue_id)
{
    string *clues;

    if (!stringp(clue_id) || clue_id == "")
        return 0;
    clues = me->query("listen/clues");
    if (!arrayp(clues))
        clues = ({ });
    if (member_array(clue_id, clues) != -1)
        return 0;
    clues += ({ clue_id });
    me->set("listen/clues", clues);
    return 1;
}

int main(object me, string arg)
{
    mapping spot;
    string spot_id;
    string rest;
    int roll;

    if (!objectp(me))
        return 0;
    if (!objectp(environment(me)))
        return 0;

    spot_id = "";
    if (stringp(arg) && sscanf(arg, "%s", rest) == 1)
        spot_id = trim(rest);
    spot = query_spot_in_room(me, spot_id);
    if (!mapp(spot)) {
        msg_send(me, "这里没有什么可偷听的。\n");
        return 1;
    }

    roll = random(100);
    if (roll < 70) {
        msg_send(me, spot["success_text"]);
        record_clue(me, spot["clue_id"]);
        return 1;
    }

    msg_send(me, spot["fail_text"]);
    return 1;
}

int help(object me)
{
    msg_send(me, @HELP
命令：listen（偷听）

在偷听点侧耳听上一阵，可能听到有用的闲话。

用法：
  listen           偷听当前房间唯一的偷听点
  listen <偷听点>  偷听指定对象
HELP
    );
    return 1;
}
