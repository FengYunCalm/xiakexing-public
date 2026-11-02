/**
 * @file    player_erasure_d.c
 * @brief   管理员角色注销预演服务。
 *
 * 真实角色注销需要跨账号、角色存档和共享服务状态的原子事务。本服务先只
 * 提供受限的只读预演，列出未来注销所需的快照与清理边界；没有任何执行
 * 删除、归档或写盘的公开接口。
 */

#pragma strict_types

#include <system/account.h>
#include <system/globals.h>
#include <system/services.h>

inherit "/lib/framework/daemon";

private int is_safe_player_id(string player_id)
{
    int index;
    int length;

    if (!stringp(player_id) || player_id == "")
        return 0;
    length = strlen(player_id);
    if (length < ACCOUNT_MIN_USERNAME_LENGTH || length > ACCOUNT_MAX_USERNAME_LENGTH)
        return 0;
    for (index = 0; index < length; index++) {
        int character;

        character = player_id[index];
        if (!((character >= 'a' && character <= 'z')
            || (character >= 'A' && character <= 'Z')
            || (character >= '0' && character <= '9')
            || character == '_'))
            return 0;
    }
    return 1;
}

private int is_disposable_player_id(string player_id)
{
    string normalized;
    string prefix;

    if (!is_safe_player_id(player_id))
        return 0;
    normalized = lower_case(player_id);
    foreach (prefix in ({ "test_", "loadtest_", "sandbox_", "disposable_" })) {
        if (strsrch(normalized, prefix) == 0)
            return 1;
    }
    return 0;
}

private object query_online_player(string player_id)
{
    if (!is_safe_player_id(player_id))
        return 0;
    return find_player_online(player_id);
}

private mapping query_file_snapshot(string path, string kind)
{
    int bytes;

    bytes = stringp(path) && path != "" ? file_size(path) : -1;
    return ([
        "kind": kind,
        "path": path,
        "present": bytes >= 0 ? 1 : 0,
        "bytes": bytes >= 0 ? bytes : 0,
    ]);
}

private mapping query_snapshot_candidates(string player_id)
{
    string shard;

    shard = sprintf("%c", player_id[0]);
    return ([
        "account": query_file_snapshot(
            ACCOUNT_DIR + shard + "/" + player_id + ".o", "account_link"),
        "character": query_file_snapshot(
            CHAR_DIR + shard + "/" + player_id + ".o", "character_save"),
        "bank": query_file_snapshot(DATA_DIR + "runtime/bank_d.o",
            "shared_runtime_store"),
        "mail": query_file_snapshot(DATA_DIR + "runtime/mail_d.o",
            "shared_runtime_store"),
        "guild": query_file_snapshot(DATA_DIR + "runtime/guild_d.o",
            "shared_runtime_store"),
        "quests": query_file_snapshot(DATA_DIR + "runtime/quest_d.o",
            "shared_runtime_store"),
        "housing": query_file_snapshot(DATA_DIR + "runtime/housing_d.o",
            "shared_runtime_store"),
        "world_state": query_file_snapshot(DATA_DIR + "runtime/world_state_d.o",
            "shared_runtime_store"),
    ]);
}

private mixed *query_planned_cleanup_actions()
{
    return ({
        ([ "id": "combat", "state": "planned",
            "description": "结束战斗并清除待结算动作与状态。" ]),
        ([ "id": "team", "state": "planned",
            "description": "取消队伍成员关系与待处理邀请。" ]),
        ([ "id": "dungeon", "state": "planned",
            "description": "退出副本运行时并清除恢复记录。" ]),
        ([ "id": "account_link", "state": "planned",
            "description": "从账号角色列表移除角色链接。" ]),
        ([ "id": "character_archive", "state": "planned",
            "description": "归档角色存档后再删除活动副本。" ]),
        ([ "id": "shared_state", "state": "planned",
            "description": "清理银行、邮件、帮派、住房、任务、世界状态和排行引用。" ]),
    });
}

private mapping rejected_preview(string error, string player_id)
{
    return ([
        "ok": 0,
        "mode": "dry_run",
        "player_id": stringp(player_id) ? player_id : "",
        "error": error,
        "execution_enabled": 0,
    ]);
}

/*
 * The caller is part of the authorization boundary. Keeping the check here
 * prevents future command aliases from accidentally exposing the preview.
 */
mapping preview_player_erasure(object operator, string player_id)
{
    mapping preview;
    object online_player;

    if (!objectp(operator) || !CONFIG_D->is_admin_player(operator))
        return rejected_preview("admin_required", player_id);
    if (!is_safe_player_id(player_id))
        return rejected_preview("invalid_player_id", player_id);
    if (!is_disposable_player_id(player_id))
        return rejected_preview("disposable_target_required", player_id);

    online_player = query_online_player(player_id);
    if (objectp(online_player))
        return rejected_preview("player_must_be_offline", player_id);
    if (!PLAYER_D->account_exists(player_id)
        && file_size(sprintf("%s%c/%s.o", CHAR_DIR, player_id[0], player_id)) < 0)
        return rejected_preview("player_not_found", player_id);

    preview = ([
        "ok": 1,
        "mode": "dry_run",
        "player_id": player_id,
        "execution_enabled": 0,
        "snapshot": query_snapshot_candidates(player_id),
        "planned_actions": query_planned_cleanup_actions(),
        "message": "预演只读取注销边界；未删除、归档或修改任何数据。",
    ]);
    return preview;
}

/* Real deletion intentionally remains unavailable until separately approved. */
mapping request_player_erasure(object operator, string player_id)
{
    mapping preview;

    preview = preview_player_erasure(operator, player_id);
    if (preview["ok"])
        preview["ok"] = 0;
    preview["error"] = "execution_disabled";
    preview["message"] = "真实角色注销尚未启用；当前只允许 dry-run 预演。";
    return preview;
}
