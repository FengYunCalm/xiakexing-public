#pragma strict_types

#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";

int main(object me, string arg);
int help(object me);

private int is_supported_activate_slot(string slot)
{
    return slot == "outer" || slot == "parry" || slot == "force" || slot == "dodge";
}

private string query_activate_target_name(string slot, mapping result)
{
    string category_name;

    if (slot == "outer") {
        category_name = mapp(result) && stringp(result["category_name"])
            ? result["category_name"] : "";
        return category_name != "" ? category_name : "当前武器招式";
    }
    switch (slot) {
        case "parry":
            return "招架";
        case "force":
            return "内功";
        case "dodge":
            return "轻功";
        default:
            return "武学";
    }
}
private string format_activate_failure(mapping result)
{
    string error;

    if (!mapp(result))
        return "你现在还不能激发这门武学。\n";

    error = result["error"];
    switch (error) {
        case "invalid_arguments":
            return "用法错误。\n";
        case "skill_not_learned":
            return "你还没有学会这门武学。\n";
        case "missing_basic_attack_profile":
            return "这门武学暂不能作为对应武学类别的招式激发。\n";
        case "unsupported_slot":
            return "这门武学不能激发到这个位置。\n";
        default:
            return "你现在还不能激发这门武学。\n";
    }
}

int main(object me, string arg)
{
    mapping result;
    mapping def;
    string skill_id;
    string skill_name;
    string slot;
    string target_name;

    if (!objectp(me))
        return 0;
    if (!stringp(arg) || arg == "")
        return help(me);

    slot = "outer";
    if (sscanf(arg, "%s %s", slot, skill_id) != 2) {
        slot = "outer";
        skill_id = arg;
    }
    if (!is_supported_activate_slot(slot)) {
        skill_id = arg;
        slot = "outer";
    }

    def = SKILL_D->get_skill_definition(skill_id);
    skill_name = mapp(def) && stringp(def["name"]) && def["name"] != "" ? def["name"] : skill_id;
    result = SKILL_BEHAVIOR_D->activate_active_skill(me, slot, skill_id);
    if (!mapp(result) || !result["ok"]) {
        msg_send(me, format_activate_failure(result));
        return 1;
    }

    target_name = query_activate_target_name(slot, result);
    msg_send(me, sprintf("你已将%s激发为%s。\n", skill_name, target_name));
    SKILL_D->xk_send_skill_event(me, ([
        "event": "activate",
        "skill_id": skill_id,
        "skill_name": skill_name,
        "slot": slot,
        "category": result["category"] || "",
        "busy": result["busy"] || 0,
    ]));
    return 1;
}

int help(object me)
{
    msg_send(me, "用法：activate [outer|parry|force|dodge] <skill_id>\n");
    return 1;
}
