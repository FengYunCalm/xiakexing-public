/**
 * @file    attribute_d.c
 * @brief   属性服务真源

 * @note
 * 本文件负责基础属性读取、应用值叠加和属性摘要输出。
 * 装备加成来源、buff 临时修正来源、角色成长分配来源仍在对象字段
 * 或其他服务中，本文件只负责统一读取与写入入口，不另存第二份属性状态。



 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

#undef is_player

inherit "/lib/framework/daemon";

int query_base_attr(object who, string attr)
{
    if (!objectp(who) || !stringp(attr))
        return 0;

    switch (attr) {
        case "str": return who->query("attributes/strength") || who->query("str") || 10;
        case "int": return who->query("attributes/intelligence") || who->query("int") || 10;
        case "con": return who->query("attributes/constitution") || who->query("con") || 10;
        case "dex": return who->query("attributes/dexterity") || who->query("dex") || 10;
        case "per": return who->query("attributes/perception") || who->query("per") || 10;
        case "spi": return who->query("attributes/spirit") || who->query("spi") || 10;
        case "kar": return who->query("attributes/luck") || who->query("kar") || 10;
    }
    return 0;
}

private int set_attribute_value(object who, string path, int value)
{
    mapping change;
    mapping response;

    if (!objectp(who))
        return 0;
    if (!function_exists("is_player", who) || !who->is_player())
        return who->set(path, value);
    change = ([ path: value ]);
    response = PLAYER_STATE_D->commit_state_fields(who, change,
        PLAYER_STATE_D->build_state_idempotency_key(who,
            "attribute/set", change));
    return mapp(response) && response["ok"];
}

int query_attr(object who, string attr)
{
    int base;
    int apply;

    if (!objectp(who) || !stringp(attr))
        return 0;
    base = query_base_attr(who, attr);
    apply = who->query("apply/" + attr) || 0;
    return base + apply;
}

void set_attr(object who, string attr, int value)
{
    mapping resource_impact;
    string numeric_attribute;

    if (!objectp(who) || !stringp(attr))
        return;
    switch (attr) {
        case "str": set_attribute_value(who, "attributes/strength", value); break;
        case "int": set_attribute_value(who, "attributes/intelligence", value); break;
        case "con": set_attribute_value(who, "attributes/constitution", value); break;
        case "dex": set_attribute_value(who, "attributes/dexterity", value); break;
        case "per": set_attribute_value(who, "attributes/perception", value); break;
        case "spi": set_attribute_value(who, "attributes/spirit", value); break;
        case "kar": set_attribute_value(who, "attributes/luck", value); break;
    }
    switch (attr) {
        case "con": numeric_attribute = "constitution"; break;
        case "dex": numeric_attribute = "dexterity"; break;
        case "int": numeric_attribute = "intelligence"; break;
        case "str": numeric_attribute = "strength"; break;
        default: numeric_attribute = ""; break;
    }
    resource_impact = numeric_attribute != ""
        ? NUMERIC_RULE_D->query_primary_attribute_resource_impact(numeric_attribute)
        : ([]);
    if (mapp(resource_impact)
        && (resource_impact["hp"] || resource_impact["mp"])) {
        if (function_exists("invalidate_xk_vital_maxima_projection", who))
            who->invalidate_xk_vital_maxima_projection(1);
    } else if (mapp(resource_impact) && resource_impact["ep"]
        && function_exists("invalidate_xk_vital_maxima_projection", who)) {
        who->invalidate_xk_vital_maxima_projection(0);
    }
    if (function_exists("invalidate_xk_score_projection", who))
        who->invalidate_xk_score_projection();
}

void add_attr(object who, string attr, int delta)
{
    set_attr(who, attr, query_base_attr(who, attr) + delta);
}

string query_attribute_desc(int value)
{
    if (value >= 25) return "超群";
    if (value >= 20) return "优秀";
    if (value >= 15) return "良好";
    if (value >= 10) return "普通";
    return "较弱";
}

string query_attribute_summary(object who)
{
    if (!objectp(who))
        return "";
    return sprintf("力量:%d(%s) 智力:%d(%s) 体质:%d(%s) 敏捷:%d(%s) 容貌:%d(%s)",
        query_attr(who, "str"), query_attribute_desc(query_attr(who, "str")),
        query_attr(who, "int"), query_attribute_desc(query_attr(who, "int")),
        query_attr(who, "con"), query_attribute_desc(query_attr(who, "con")),
        query_attr(who, "dex"), query_attribute_desc(query_attr(who, "dex")),
        query_attr(who, "per"), query_attribute_desc(query_attr(who, "per")));
}
