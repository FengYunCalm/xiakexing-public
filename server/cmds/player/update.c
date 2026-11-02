/**
 * @file    update.c
 * @brief   热更新命令



 *
 * @par Description:
 * 热更新指定文件，原子替换已加载对象的程序，保留对象身份与状态。
 * 支持更新房间、NPC、物品等。
 *
 * @par Usage:
 *   update <文件路径>  - 更新指定文件
 *   update here        - 更新当前房间
 *   update /path/to/file.c - 更新绝对路径文件
 *
 * @par Examples:
 *   update here                   - 更新当前房间
 *   update /cmds/player/hp.c - 更新hp命令
 *   update /kernel/login.c   - 更新login对象
 *
 */

#pragma strict_types

#include <system/ansi.h>
#include <game/command.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";

#define UPDATE_D "/daemons/services/system/update_d"

int main(object me, string arg);
int help(object me);

private nosave object _update_service;

private object load_update_service()
{
    object update_d;

    if (objectp(_update_service))
        return _update_service;

    update_d = find_object(UPDATE_D);
    if (!objectp(update_d))
        update_d = load_object(UPDATE_D);

    if (objectp(update_d))
        _update_service = update_d;

    return update_d;
}

/**
 * @brief 热更新命令主函数
 * @param me 玩家对象
 * @param arg 命令参数
 * @return 1=请求已处理（执行结果见消息和日志），0=拒绝
 *
 * 使用 Guard Clauses 与子函数拆分控制复杂度。
 */
int main(object me, string arg)
{
    object update_d;

    if (!objectp(me))
        return 0;
    if (!is_admin_operator(me)) {
        msg_send(me, "你没有权限执行此命令。\n");
        return 0;
    }

    update_d = load_update_service();
    if (!objectp(update_d))
        return 0;

    return update_d->request_update(me, arg);
}

int help(object me)
{
    msg_send(me, @HELP
命令：update（热更新）

用途：
  热更新LPC对象，无需重启服务器即可应用代码修改。
  实际可用范围以当前服务端权限策略为准。

用法：
  update <文件路径>       - 更新指定文件
  update here            - 更新当前房间
  update /path/to/file   - 更新绝对路径文件

示例：
  update here                    - 更新当前房间
  update /cmds/player/hp.c - 更新hp命令
  update /kernel/login.c   - 更新login对象
  update /world/<region>/rooms/<room_id>.c - 更新房间

注意：
  - 当前房间有在线玩家时不会执行房间热更
  - 已加载对象使用原子重编译，失败保留旧程序，不销毁重载
  - 普通对象不重跑 create；旧函数指针不会自动重绑
  - master、simul_efun 和更新服务自身须通过标准重启入口更新
  - 请求受理后由服务完成；发起者离线仍保留结果日志
  - 编译错误会显示详细信息

相关命令：
  look - 查看环境
  go   - 移动

HELP
    );
    return 1;
}
