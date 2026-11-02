#pragma strict_types

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

int main(object me, string arg);
int help(object me);

private int show_buffs_public(object me)
{
    if (!function_exists("show_buffs", me)) {
        msg_send(me, "当前角色没有状态效果能力。\n");
        return 1;
    }
    me->show_buffs();
    return 1;
}

private int do_clear(object me, string rest)
{
    if (!stringp(rest) || rest == "") {
        msg_send(me, "用法：buff clear <poison|debuff|all>\n");
        return 1;
    }

    switch (rest) {
        case "poison":
            me->cure_poison();
            msg_send(me, "你尝试清除身上的中毒状态。\n");
            return 1;
        case "debuff":
            me->cure_debuff();
            msg_send(me, "你尝试清除身上的减益状态。\n");
            return 1;
        case "all":
            if (function_exists("clear_all_buffs", me))
                me->clear_all_buffs();
            msg_send(me, "你清除了身上的所有状态效果。\n");
            return 1;
        default:
            msg_send(me, "只能清除 poison / debuff / all。\n");
            return 1;
    }
}

int main(object me, string arg)
{
    mapping parsed;

    if (!objectp(me))
        return 0;
    if (!stringp(arg) || arg == "")
        return show_buffs_public(me);

    parsed = parse_subcommand_args(arg);
    if (!parsed["valid"])
        return help(me);

    if (parsed["action"] == "clear")
        return do_clear(me, parsed["rest"]);

    return help(me);
}

int help(object me)
{
    msg_send(me, "用法：buff [clear <poison|debuff|all>]\n");
    return 1;
}
