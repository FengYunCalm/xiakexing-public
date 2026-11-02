#pragma strict_types

#include <system/services.h>

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

int main(object me, string arg);
int help(object me);

private int do_enter(object me, string rest)
{
    mapping result;

    if (!stringp(rest) || rest == "") {
        msg_send(me, "用法：housing enter <房产ID>\n");
        return 1;
    }

    result = HOUSING_D->enter_property(me, rest);
    msg_send(me, mapp(result) ? (result["message"] || "进入失败。\n") : "进入失败。\n");
    return 1;
}

private int do_leave(object me)
{
    mapping result;

    result = HOUSING_D->leave_property(me);
    msg_send(me, mapp(result) ? (result["message"] || "离开失败。\n") : "离开失败。\n");
    return 1;
}

private int do_guest(object me, string rest)
{
    mapping parsed;
    mapping result;

    parsed = parse_command_args(rest, "%s list");
    if (parsed["valid"]) {
        msg_send(me, HOUSING_D->render_guest_list(me, parsed["args"][0]));
        return 1;
    }

    parsed = parse_command_args(rest, "%s add %s");
    if (parsed["valid"]) {
        result = HOUSING_D->set_guest_access(me, parsed["args"][0], parsed["args"][1], 1);
        msg_send(me, mapp(result) ? (result["message"] || "设置访客失败。\n") : "设置访客失败。\n");
        return 1;
    }

    parsed = parse_command_args(rest, "%s remove %s");
    if (parsed["valid"]) {
        result = HOUSING_D->set_guest_access(me, parsed["args"][0], parsed["args"][1], 0);
        msg_send(me, mapp(result) ? (result["message"] || "移除访客失败。\n") : "移除访客失败。\n");
        return 1;
    }

    msg_send(me, "用法：housing guest <房产ID> <list|add 玩家ID|remove 玩家ID>\n");
    return 1;
}

private int do_list(object me, string rest)
{
    int page;

    page = 1;
    if (stringp(rest) && sscanf(rest, "%d", page) != 1)
        page = 1;
    msg_send(me, HOUSING_D->render_catalog(me, page));
    return 1;
}

private int do_status(object me)
{
    msg_send(me, HOUSING_D->render_status(me));
    return 1;
}

private int do_rent(object me, string rest)
{
    mapping result;

    if (!stringp(rest) || rest == "") {
        msg_send(me, "用法：housing rent <房产ID>\n");
        return 1;
    }

    result = HOUSING_D->rent_property(me, rest);
    msg_send(me, mapp(result) ? (result["message"] || "租赁失败。\n") : "租赁失败。\n");
    return 1;
}

private int do_renew(object me, string rest)
{
    mapping result;

    if (!stringp(rest) || rest == "") {
        msg_send(me, "用法：housing renew <房产ID>\n");
        return 1;
    }

    result = HOUSING_D->renew_property(me, rest);
    msg_send(me, mapp(result) ? (result["message"] || "续租失败。\n") : "续租失败。\n");
    return 1;
}

private int do_release(object me, string rest)
{
    mapping result;

    if (!stringp(rest) || rest == "") {
        msg_send(me, "用法：housing release <房产ID>\n");
        return 1;
    }

    result = HOUSING_D->release_property(me, rest);
    msg_send(me, mapp(result) ? (result["message"] || "释放失败。\n") : "释放失败。\n");
    return 1;
}

private int do_storage(object me, string rest)
{
    mapping parsed;
    int page;

    parsed = parse_command_args(rest, "%s %d");
    if (parsed["valid"]) {
        msg_send(me, HOUSING_D->render_storage(me, parsed["args"][0], parsed["args"][1]));
        return 1;
    }

    parsed = parse_command_args(rest, "%s");
    if (!parsed["valid"]) {
        msg_send(me, "用法：housing storage <房产ID> [页码]\n");
        return 1;
    }

    page = 1;
    msg_send(me, HOUSING_D->render_storage(me, parsed["args"][0], page));
    return 1;
}

private int do_store(object me, string rest)
{
    mapping parsed;
    mapping result;

    parsed = parse_command_args(rest, "%s %s");
    if (!parsed["valid"]) {
        msg_send(me, "用法：housing store <房产ID> <物品>\n");
        return 1;
    }

    result = HOUSING_D->store_item(me, parsed["args"][0], parsed["args"][1]);
    msg_send(me, mapp(result) ? (result["message"] || "存入失败。\n") : "存入失败。\n");
    return 1;
}

private int do_take(object me, string rest)
{
    mapping parsed;
    mapping result;

    parsed = parse_command_args(rest, "%s %d");
    if (!parsed["valid"]) {
        msg_send(me, "用法：housing take <房产ID> <序号>\n");
        return 1;
    }

    result = HOUSING_D->take_item(me, parsed["args"][0], parsed["args"][1]);
    msg_send(me, mapp(result) ? (result["message"] || "取出失败。\n") : "取出失败。\n");
    return 1;
}

int main(object me, string arg)
{
    mapping parsed;

    if (!objectp(me))
        return 0;
    if (!stringp(arg) || arg == "")
        return do_status(me);

    parsed = parse_subcommand_args(arg);
    if (!parsed["valid"])
        return help(me);

    switch (parsed["action"]) {
        case "list":
            return do_list(me, parsed["rest"]);
        case "status":
            return do_status(me);
        case "rent":
            return do_rent(me, parsed["rest"]);
        case "renew":
            return do_renew(me, parsed["rest"]);
        case "release":
            return do_release(me, parsed["rest"]);
        case "storage":
            return do_storage(me, parsed["rest"]);
        case "store":
            return do_store(me, parsed["rest"]);
        case "take":
            return do_take(me, parsed["rest"]);
        case "enter":
            return do_enter(me, parsed["rest"]);
        case "leave":
            return do_leave(me);
        case "guest":
            return do_guest(me, parsed["rest"]);
        default:
            return help(me);
    }
}

int help(object me)
{
    msg_send(me, @HELP
命令：housing（房屋）

用法：
  housing list [页码]
  housing status
  housing rent <房产ID>
  housing renew <房产ID>
  housing release <房产ID>
  housing enter <房产ID>
  housing leave
  housing guest <房产ID> <list|add 玩家ID|remove 玩家ID>
  housing storage <房产ID> [页码]
  housing store <房产ID> <物品>
  housing take <房产ID> <序号>

说明：
  - 当前版本先开放租赁、房税、基础仓储与室内访问
  - 房税与续租均按当前 economy 配置结算
  - 房屋仓储使用附件式保存，不是全局公共仓库
HELP
    );
    return 1;
}
