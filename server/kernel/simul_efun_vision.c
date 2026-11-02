/**
 * @file    simul_efun_vision.c
 * @brief   视觉消息辅助子服务
 * @author  XiaKeXing Development Team
 * @date    2026-03-30
 * @version 1.0.0
 */

#pragma strict_types

string gender_self(string sex)
{
    return "你";
}

string gender_pronoun(string sex)
{
    if (!stringp(sex))
        return "他";

    switch (sex) {
    case "女性":
    case "女":
        return "她";
    case "雄性":
    case "雌性":
    case "动物":
        return "它";
    default:
        return "他";
    }
}

private void send_vision_message(mixed target, string text)
{
    object ob;

    if (!stringp(text) || text == "")
        return;
    if (objectp(target)) {
        call_other("/kernel/simul_efun", "msg_send", target, text);
        return;
    }
    if (!arrayp(target))
        return;
    foreach (ob in target) {
        if (objectp(ob))
            call_other("/kernel/simul_efun", "msg_send", ob, text);
    }
}

varargs void message_vision(string msg, object me, object you)
{
    string str1;
    string str2;
    string str3;
    string my_name;
    string your_name;
    string my_gender;
    string your_gender;

    if (!stringp(msg) || !objectp(me))
        return;

    catch { my_name = me->name(); };
    if (!stringp(my_name))
        my_name = "某人";

    catch { my_gender = me->query("gender"); };
    if (!stringp(my_gender))
        my_gender = "男性";

    str1 = replace_string(msg, "$P", gender_self(my_gender));
    str1 = replace_string(str1, "$N", gender_self(my_gender));

    str3 = replace_string(msg, "$P", my_name);
    str3 = replace_string(str3, "$N", my_name);

    if (objectp(you)) {
        catch { your_name = you->name(); };
        if (!stringp(your_name))
            your_name = "某人";

        catch { your_gender = you->query("gender"); };
        if (!stringp(your_gender))
            your_gender = "男性";

        str1 = replace_string(str1, "$p", your_name);
        str1 = replace_string(str1, "$n", your_name);

        str3 = replace_string(str3, "$p", your_name);
        str3 = replace_string(str3, "$n", your_name);

        str2 = replace_string(msg, "$P", gender_pronoun(my_gender));
        str2 = replace_string(str2, "$N", my_name);
        str2 = replace_string(str2, "$p", gender_self(your_gender));
        str2 = replace_string(str2, "$n", gender_self(your_gender));

        send_vision_message(you, str2);
    }

    send_vision_message(me, str1);

    if (environment(me)) {
        object *inv;

        inv = all_inventory(environment(me));
        inv -= ({ me });
        if (you)
            inv -= ({ you });
        if (sizeof(inv) > 0)
            send_vision_message(inv, str3);
    }
}
