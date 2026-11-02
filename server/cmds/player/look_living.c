/**
 * @file    look_living.c
 * @brief   查看人物模块 - Living Viewing



 */

#pragma strict_types

#include <system/ansi.h>
#include <system/errors.h>
#include <framework/message_facade.h>
#include <system/services.h>

inherit "/lib/std/utils/util_status";
inherit "/lib/std/utils/util_player";

string get_appearance_desc(object ob);
string get_action_hints(object me, object ob);

/**
 * @brief 根据年龄获取描述
 * @param age 年龄
 * @param pro 代词("你"或"他")
 * @return 年龄描述字符串
 */
private string get_age_desc(int age, string pro)
{
    if (age < 20) return pro + "看起来年纪尚轻。\n";
    if (age < 30) return pro + "看起来二十多岁。\n";
    if (age < 40) return pro + "看起来三十多岁。\n";
    if (age < 50) return pro + "看起来四十多岁。\n";
    return pro + "看起来年过半百。\n";
}

/**
 * @brief 根据气血百分比获取状态描述
 * @param percent 气血百分比 (0-100)
 * @return 状态描述字符串
 */
private string get_health_status(int percent)
{
    if (percent >= 100) return HIG "精力充沛" NOR;
    if (percent >= 80) return HIY "状态良好" NOR;
    if (percent >= 50) return YEL "略显疲惫" NOR;
    if (percent >= 20) return RED "伤痕累累" NOR;
    return HIR "命悬一线" NOR;
}

/**
 * @brief 构建基础信息字符串
 * @param ob 目标对象
 * @return 格式化后的名字和头衔
 */
private string build_basic_info(object ob)
{
    string display_name;
    string str;

    display_name = get_object_display_name(ob, "某人");
    str = HIC + display_name + NOR;

    if (ob->query("title")) {
        str += " " + (stringp(ob->query("title")) ? ob->query("title") : "");
    }

    str += "\n";
    str += "────────────────────────\n";

    return str;
}

/**
 * @brief 构建年龄描述
 * @param ob 目标对象
 * @param pro 代词
 * @return 年龄描述字符串
 */
private string build_age_desc(object ob, string pro)
{
    int age = ob->query("age");
    return age ? get_age_desc(age, pro) : "";
}

private string build_long_desc(object ob)
{
    string long_desc;

    long_desc = ob->query_long();
    if (!stringp(long_desc) || long_desc == "")
        long_desc = ob->query("long") || "";
    if (!stringp(long_desc) || long_desc == "")
        return "";

    if (long_desc[<1] != '\n')
        long_desc += "\n";

    return long_desc;
}

/**
 * @brief 构建成长修为描述
 * @param ob 目标对象
 * @return 成长修为字符串
 */
private string build_combat_desc(object ob)
{
    int progression_rank;
    int skill_cap;

    if (!objectp(ob))
        return "成长修为：不明\n";

    progression_rank = NUMERIC_RULE_D->query_player_progression_rank(ob);
    skill_cap = NUMERIC_RULE_D->query_player_exp_skill_cap(ob);

    return sprintf("成长修为：档位%d，技能上限%d\n", progression_rank, skill_cap);
}

/**
 * @brief 构建健康状态描述
 * @param ob 目标对象
 * @return 健康状态字符串
 */
private string build_health_desc(object ob)
{
    int max_hp;
    int hp;

    max_hp = ob->query("combat/max_hp");
    hp = ob->query("combat/hp");

    if (!intp(max_hp) || max_hp <= 0 || !intp(hp) || hp < 0) return "";

    int percent = hp * 100 / max_hp;
    return "身体状况：" + get_health_status(percent) + "\n";
}

/**
 * @brief 构建门派信息
 * @param ob 目标对象
 * @return 门派信息字符串
 */
private string build_family_info(object ob)
{
    string family = ob->query("family/family_name");

    if (!family) return "";

    return "所属门派：" + (stringp(family) ? family : "无") + "\n";
}

/**
 * @brief 发送观察消息
 * @param me 观察者
 * @param ob 被观察者
 */
private void send_observe_messages(object me, object ob)
{
    string observer_name;
    string target_name;

    if (ob == me || !living(ob)) return;

    observer_name = get_object_display_name(me, "有人");
    target_name = get_object_display_name(ob, "对方");

    msg_send(ob, observer_name + "正在仔细打量着你。\n");
    msg_to_room(environment(me), observer_name + "正在仔细打量着" + target_name + "。\n",
                ({ me, ob }));
}

/**
 * @brief 构建互动提示
 * @param me 观察者
 * @param ob 被观察者
 * @return 互动提示字符串
 */
private string build_action_hints(object me, object ob)
{
    string hints = get_action_hints(me, ob);

    if (hints == "") return "";

    return HIC "【可进行的互动】\n" NOR + hints;
}

/**
 * @brief 构建分隔线
 * @return 分隔线字符串
 */
private string get_separator()
{
    return "────────────────────────\n";
}

/**
 * @brief 观察人物
 */
void look_living(object me, object ob)
{
    string str, pro;

    if (!me || !ob) {
        msg_send(me, "你什么也看不到。\n");
        return;
    }

    pro = (ob == me) ? "你" : "他";

    // 构建基本信息
    str = build_basic_info(ob);
    str += build_long_desc(ob);
    str += build_age_desc(ob, pro);
    str += build_combat_desc(ob);

    // 外观描述
    string appearance = get_appearance_desc(ob);
    str += pro + appearance + "。\n";

    // 健康状态
    str += build_health_desc(ob);

    // 门派信息
    str += build_family_info(ob);

    // 发送观察消息
    send_observe_messages(me, ob);

    // 结束分隔线
    str += get_separator();

    // 互动提示
    str += build_action_hints(me, ob);

    msg_send(me, str);
}
