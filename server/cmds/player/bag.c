#pragma strict_types

int main(object me, string arg)
{
    return call_other("/cmds/player/inventory", "main", me, arg);
}

int help(object me)
{
    return call_other("/cmds/player/inventory", "help", me);
}
