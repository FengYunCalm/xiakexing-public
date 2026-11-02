/**
 * @file    call_dispatch.c
 * @brief   调用分发工具



 *
 * @par Description:
 * 框架层辅助模块。
 * 统一承接 callback 分发，减少宿主对象重复展开可变参数的样板代码。
 */

#pragma strict_types
#pragma save_types

void dispatch_call(object owner, string callback, mixed *args)
{
    /*
     * owner 是回调真正归属方，这里只负责安全分发，
     * 不替宿主吞掉不存在函数或权限语义带来的更深层问题。
     */
    if (!objectp(owner) || !stringp(callback) || callback == "")
        return;

    /* 小参数量显式展开，优先兼容旧驱动/旧写法下对 varargs 展开的保守依赖。 */
    if (!arrayp(args) || sizeof(args) == 0) {
        call_other(owner, callback);
        return;
    }

    switch (sizeof(args)) {
    case 1:
        call_other(owner, callback, args[0]);
        break;
    case 2:
        call_other(owner, callback, args[0], args[1]);
        break;
    case 3:
        call_other(owner, callback, args[0], args[1], args[2]);
        break;
    case 4:
        call_other(owner, callback, args[0], args[1], args[2], args[3]);
        break;
    default:
        call_other(owner, callback, args...);
        break;
    }
}
