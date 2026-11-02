/**
 * @file    simul_efun_object_identity.c
 * @brief   对象身份工具辅助子服务
 * @author  XiaKeXing Development Team
 * @date    2026-03-30
 * @version 1.0.0
 */

#pragma strict_types

varargs string get_object_id(object ob);

string real_id(object ob)
{
    if (!ob)
        return "";

    return get_object_id(ob);
}

varargs string get_object_id(object ob)
{
    mixed ids;

    if (!ob)
        ob = this_object();
    if (!ob)
        return "";

    if (function_exists("query_id", ob)) {
        ids = call_other(ob, "query_id");
        if (stringp(ids))
            return ids;
        if (arrayp(ids) && sizeof(ids) > 0)
            return ids[0];
    }

    ids = ob->query("id");
    if (stringp(ids))
        return ids;
    if (arrayp(ids) && sizeof(ids) > 0)
        return ids[0];

    ids = ob->query("account_id");
    if (stringp(ids))
        return ids;

    return "";
}

varargs string get_object_name(object ob)
{
    string name;

    if (!ob)
        ob = this_object();
    if (!ob)
        return "";

    if (function_exists("query_name", ob)) {
        name = call_other(ob, "query_name");
        if (stringp(name) && name != "")
            return name;
    }

    name = ob->query("name");
    if (stringp(name) && name != "")
        return name;

    name = get_object_id(ob);
    if (stringp(name) && name != "")
        return name;

    return "";
}

int userp(object ob)
{
    return ob && interactive(ob);
}

int playerp(object ob)
{
    return ob && userp(ob);
}
