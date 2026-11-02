#pragma strict_types

#include <system/services.h>
#include <system/xk_protocol.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

int main(object me, string arg);
int help(object me);

private object resolve_manual_in_inventory(object me, string manual_name)
{
    object item;

    item = find_item_in_player(me, manual_name);
    if (objectp(item))
        return item;

    foreach (item in all_inventory(me)) {
        if (!objectp(item))
            continue;
        if ((item->query_name() || item->query("name") || "") == manual_name)
            return item;
    }

    return 0;
}

private string format_study_failure(mapping result)
{
    string error;

    if (!mapp(result))
        return "你暂时无法从这本书里学到什么。\n";

    error = result["error"];
    switch (error) {
        case "missing_manual_source":
            return "这本书里没有可供研读的武学内容。\n";
        case "missing_manual_skill":
            return "这本书的武学内容残缺不全。\n";
        case "exp_too_low":
            return "你的实战经验不足，暂时看不懂这本秘籍。\n";
        case "min_skill_not_met":
            return "你的基础火候还不够，暂时无法研读这本秘籍。\n";
        case "manual_limit_reached":
            return "这本秘籍对你来说已经太浅了。\n";
        case "study_failed":
            return "你反复研读，却始终抓不到要领。\n";
        case "insufficient_potential":
            return "你的潜能不足，暂时付不起这次研读消耗。\n";
        case "insufficient_combat_exp":
            return "你的实战经验不足，暂时付不起这次研读消耗。\n";
        case "insufficient_sect_contribution":
            return "你的门派贡献不足，暂时付不起这次研读消耗。\n";
        case "insufficient_guild_contribution":
            return "你的帮派贡献不足，暂时付不起这次研读消耗。\n";
        case "spend_failed":
            return "研读消耗结算失败，请稍后再试。\n";
        default:
            return "你暂时无法从这本书里学到什么。\n";
    }
}

private string query_study_failure_reason(mapping result)
{
    if (mapp(result) && stringp(result["error"]) && result["error"] != "")
        return result["error"];
    return "study_failed";
}

private void send_study_error(object me, string message, string reason, string request, int code)
{
    xk_send_command_error(me, message, code, "study", reason, request,
        CLIENT_SYNC_D->capture_command_response_context(me));
    msg_error(me, message, "study", reason, request, code);
}

int main(object me, string arg)
{
    mapping result;
    object manual;
    string manual_name;

    if (!objectp(me))
        return 0;
    if (!stringp(arg) || arg == "")
        return help(me);

    manual_name = arg;
    manual = resolve_manual_in_inventory(me, manual_name);
    if (!objectp(manual)) {
        send_study_error(me, "你身上没有这本秘籍。\n", "manual_not_found", manual_name, XK_ERR_NOT_FOUND);
        return 1;
    }

    result = SKILL_SOURCE_D->study_from_manual(me, manual);
    if (mapp(result) && result["ok"]) {
        msg_system(me, sprintf("你从%s中悟到了%s。\n", manual->query_name() || "秘籍", result["skill_id"] || "武学"),
            "skill_study");
        return 1;
    }

    send_study_error(me, format_study_failure(result), query_study_failure_reason(result),
        manual_name, XK_ERR_SKILL_NOT_FOUND);
    return 1;
}

int help(object me)
{
    msg_send(me, "用法：study <秘籍名>\n");
    return 1;
}
