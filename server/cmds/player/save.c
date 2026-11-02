/**
 * @file    save.c
 * @brief   保存命令 - 保存玩家当前状态



 *
 * @par Description:
 * 保存玩家当前状态，发送XK协议消息。
 *
 * @par Usage:
 *   save              - 保存当前角色
 *
 */

#pragma strict_types

#include <system/ansi.h>
#include <system/services.h>
#include <game/command.h>
#include <framework/message_facade.h>

inherit "/lib/framework/network";

private void emit_save_dialog(object me, string content,
    mapping response_context)
{
    if (!objectp(me) || !stringp(content) || content == "")
        return;

    xk_send_dialog(me, "保存", content,
        DIALOG_PROJECTION_D->build_dialog_extra("save", "save_panel", ([
            "replace_mode": "replace",
            "actions": ({
                (["label":"查看状态", "cmd":"score"]),
                (["label":"服务器状态", "cmd":"status"]),
                (["label":"查看帮助", "cmd":"help save"]),
            }),
        ])), response_context);
}

/**
 * 命令主入口
 */
int main(object me, string arg)
{
    mapping response_context;
    mapping save_phase;
    mixed save_error;

    if (!me) return 0;
    response_context = CLIENT_SYNC_D->capture_command_response_context(me);

    /* 保存玩家数据：只有完整收据 committed 才发送成功反馈。 */
    save_error = catch {
        if (!function_exists("save_phases", me))
            error("player save phases unavailable");
        save_phase = me->save_phases();
    };
    if (!save_error && mapp(save_phase)
        && save_phase["outcome"] == "committed") {
        if (!is_xk_client(me))
            msg_send(me, HIG "【系统】角色数据保存成功！" NOR "\n", response_context);
        emit_save_dialog(me, "角色数据保存成功！", response_context);

        /* 发送 XK 协议状态更新 */
        send_xk_status(me, response_context);

        /* 发送快捷操作 */
        send_xk_quick_cmds(me,
            "系统|查看状态:score|继续游戏:look",
            1, response_context);
    } else {
        if (!is_xk_client(me))
            msg_send(me, HIR "【系统】保存失败，请稍后重试。" NOR "\n", response_context);
        emit_save_dialog(me,
            mapp(save_phase) && save_phase["outcome"] == "persisted_receipt_pending"
                ? "保存已写入，收据尚未完成，请稍后重试。"
                : "保存失败，请稍后重试。", response_context);
    }

    return 1;
}

/**
 * 帮助信息
 */
int help(object me)
{
    msg_send(me, @HELP
命令：save（保存）

保存你当前的角色数据。

用法：
  save              保存当前角色

注意：
  - 系统会自动定期保存
  - 重要操作后建议手动保存
  - 退出前保存可以确保数据不丢失

相关命令：
  score   - 查看角色状态
  status  - 查看状态简报

HELP
    );
    return 1;
}
