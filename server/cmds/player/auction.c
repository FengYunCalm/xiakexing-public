#pragma strict_types

#include <system/services.h>

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

int main(object me, string arg);
int help(object me);

private int do_list(object me, string rest)
{
    int page;

    page = 1;
    if (stringp(rest) && sscanf(rest, "%d", page) != 1)
        page = 1;
    msg_send(me, AUCTION_D->render_public_listings(page));
    return 1;
}

private int do_sell(object me, string rest)
{
    mapping parsed;
    mapping result;
    int duration;
    int price;
    string item_name;

    parsed = parse_command_args(rest, "%s %d %d");
    if (parsed["valid"]) {
        item_name = parsed["args"][0];
        price = parsed["args"][1];
        duration = parsed["args"][2];
    } else {
        parsed = parse_command_args(rest, "%s %d");
        if (!parsed["valid"]) {
            msg_send(me, "用法：auction sell <物品> <底价> [时长]\n");
            return 1;
        }
        item_name = parsed["args"][0];
        price = parsed["args"][1];
        duration = 0;
    }

    result = AUCTION_D->create_listing(me, item_name, price, duration);
    msg_send(me, mapp(result) && result["success"]
        ? sprintf("拍卖创建成功：%s，保证金 %s。\n", result["listing_id"], MONEY_D->format_money(result["deposit"] || 0))
        : (mapp(result) ? (result["message"] || "拍卖创建失败。\n") : "拍卖创建失败。\n"));
    return 1;
}

private int do_bid(object me, string rest)
{
    mapping parsed;
    mapping result;

    parsed = parse_command_args(rest, "%s %d");
    if (!parsed["valid"]) {
        msg_send(me, "用法：auction bid <拍卖ID> <金额>\n");
        return 1;
    }

    result = AUCTION_D->place_bid(me, parsed["args"][0], parsed["args"][1]);
    msg_send(me, mapp(result) && result["success"]
        ? sprintf("出价成功，当前价格 %s。\n", MONEY_D->format_money(result["current_bid"] || 0))
        : (mapp(result) ? (result["message"] || "出价失败。\n") : "出价失败。\n"));
    return 1;
}

private int do_cancel(object me, string rest)
{
    mapping result;

    if (!stringp(rest) || rest == "") {
        msg_send(me, "用法：auction cancel <拍卖ID>\n");
        return 1;
    }

    result = AUCTION_D->cancel_listing(me, rest);
    msg_send(me, mapp(result) && result["success"]
        ? "拍卖已取消，物品与保证金已发放。\n"
        : (mapp(result) ? (result["message"] || "取消拍卖失败。\n") : "取消拍卖失败。\n"));
    return 1;
}

private int do_mine(object me)
{
    msg_send(me, AUCTION_D->render_player_listings(me));
    return 1;
}

private int do_claim(object me, string rest)
{
    mapping result;

    if (!stringp(rest) || rest == "") {
        msg_send(me, "用法：auction claim <拍卖ID>\n");
        return 1;
    }

    result = AUCTION_D->claim_listing(me, rest);
    msg_send(me, mapp(result) && result["success"]
        ? "拍卖结算已重新处理。\n"
        : (mapp(result) ? (result["message"] || "领取失败。\n") : "领取失败。\n"));
    return 1;
}

int main(object me, string arg)
{
    mapping parsed;

    if (!objectp(me))
        return 0;
    if (!stringp(arg) || arg == "")
        return do_list(me, "");

    parsed = parse_subcommand_args(arg);
    if (!parsed["valid"])
        return help(me);

    switch (parsed["action"]) {
        case "list": return do_list(me, parsed["rest"]);
        case "sell": return do_sell(me, parsed["rest"]);
        case "bid": return do_bid(me, parsed["rest"]);
        case "cancel": return do_cancel(me, parsed["rest"]);
        case "mine": return do_mine(me);
        case "claim": return do_claim(me, parsed["rest"]);
        default: return help(me);
    }
}

int help(object me)
{
    msg_send(me, @HELP
命令：auction（拍卖）

用法：
  auction list [页码]
  auction sell <物品> <底价> [时长]
  auction bid <拍卖ID> <金额>
  auction cancel <拍卖ID>
  auction mine
  auction claim <拍卖ID>
HELP
    );
    return 1;
}
