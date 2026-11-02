/**
 * @file    look_hints.c
 * @brief   查看提示辅助 - 生成环境提示文案



 */

#pragma strict_types

string get_appearance_desc(object ob)
{
    int per;
    string gender;

    if (!objectp(ob))
        return "相貌平平";

    per = ob->query_per() || 15;
    gender = ob->query("gender") || "男性";

    if (gender == "女性") {
        if (per >= 30) return "天生丽质";
        if (per >= 20) return "面目清秀";
        return "相貌平平";
    }

    if (per >= 30) return "气宇轩昂";
    if (per >= 20) return "面目清秀";
    return "相貌平平";
}

string get_action_hints(object me, object ob)
{
    if (!objectp(me) || !objectp(ob) || ob == me)
        return "";

    return "  - 你可以观察对方的状态（look）\n";
}

string get_item_action_hints(object me, object ob)
{
    if (!objectp(me) || !objectp(ob))
        return "";

    if (environment(ob) == me)
        return "  - 你可以丢弃它（drop）\n";

    return "  - 你可以捡起它（get）\n";
}
