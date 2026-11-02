/**
 * @file    char_d.c
 * @brief   角色运行期数据入口（legacy-but-canonical）



 *
 * @par Description:
 * 管理角色创建、加载、保存等运行期入口。
 *
 * 它当前保留在 `daemons/game/` 命名空间，是一个 legacy-but-canonical
 * 的角色数据入口：登录链和旧调用侧仍可通过这里拿到“创建/加载/保存角色”的
 * 单一入口，但账号体系、角色列表与更高层玩家服务真源并不都由本文件独占。
 *
 * 当前职责：
 * - 创建/加载/保存角色对象
 * - 兼容旧入口的账号角色查询
 *
 * 不承担：
 * - 新玩家服务体系的唯一真源声明
 * - 账号认证或权限判断真源
 * - 登录状态机推进真源
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/constants.h>
#include <system/errors.h>
#include <system/daemon.h>
#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/framework/daemon";

#define LOGIN_SESSION "/kernel/login/login_session"

private string resolve_character_save_file(string account_id);
private int remove_character_save_variant(string file);
private int character_save_residue_exists(string account_id);
private int purge_character_runtime_data(string account_id);

/**
 * @brief 创建新角色
 * @param account_id 账号ID
 * @param char_name 角色名
 * @param gender 性别
 * @return 角色对象或0
 */
object create_character(string account_id, string char_name, string gender)
{
    object char_ob;
    string char_file;
    string save_dir;
    string save_file;
    mapping save_started_at;
    mapping save_phase;
    mixed save_error;
    int saved;
    int save_result;

    if (!account_id || !char_name || !gender)
        return 0;

    if (character_save_residue_exists(account_id))
        return 0;

    // 创建角色对象
    char_file = PLAYER;
    char_ob = new(char_file);

    if (!objectp(char_ob))
        return 0;

    // 初始化角色数据
    save_dir = DATA_DIR + "user/" + account_id[0..0];
    save_file = save_dir + "/" + account_id + ".o";

    if (file_size(save_dir) != -2)
        mkdir(save_dir);

    char_ob->set_save_path(save_file);
    char_ob->set_auto_save(1);
    char_ob->set_auto_save_delay(120);
    char_ob->set("id", account_id);
    char_ob->set("name", char_name);
    char_ob->set("gender", gender);
    char_ob->set("account_id", account_id);
    char_ob->set("reward/starter_grant", ([
        "schema": "xkx_starter_grant_v1",
        "status": "pending",
        "created_at": time(),
    ]));

    if (call_other(PLAYER_D, "consume_registration_test_fault",
        account_id, "character_save_before")) {
        destruct(char_ob);
        return 0;
    }

    save_started_at = call_other(LOGIN_SESSION, "begin_loadtest_login_phase_timing");
    save_error = catch {
        if (!function_exists("save_phases", char_ob))
            error("character save phases unavailable");
        save_phase = char_ob->save_phases();
        save_result = mapp(save_phase) ? save_phase["save_result"] : 0;
        saved = mapp(save_phase)
            && save_phase["outcome"] == "committed";
    };
    call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
        "character_save", save_started_at, save_result);
    if (save_error || !saved) {
        log_file("system/char_create", sprintf(
            "save failed account=%s phase=%O error=%O\n", account_id,
            save_phase, save_error));
        log_file("system/char_create", sprintf("save failed account=%s\n", account_id));
        destruct(char_ob);
        return 0;
    }

    return char_ob;
}

/**
 * @brief 加载角色
 * @param account_id 账号ID
 * @return 角色对象或0
 */
object load_character(string account_id)
{
    object char_ob;
    string char_file;
    string load_file;
    string save_file;

    if (!account_id)
        return 0;

    load_file = resolve_character_save_file(account_id);
    if (!stringp(load_file) || load_file == "")
        return 0;

    // 加载角色对象
    char_file = PLAYER;
    char_ob = new(char_file);

    if (!objectp(char_ob))
        return 0;

    save_file = DATA_DIR + "user/" + account_id[0..0] + "/" + account_id + ".o";
    char_ob->set_save_path(load_file);
    char_ob->set_auto_save(1);
    char_ob->set_auto_save_delay(120);
    char_ob->set("id", account_id);
    if (!char_ob->restore()) {
        destruct(char_ob);
        return 0;
    }
    char_ob->set_save_path(save_file);

    return char_ob;
}

private string resolve_character_save_file(string account_id)
{
    string base_file;
    string backup_file;
    string temp_file;

    if (!stringp(account_id) || account_id == "")
        return "";

    base_file = DATA_DIR + "user/" + account_id[0..0] + "/" + account_id + ".o";
    if (file_size(base_file) > 0)
        return base_file;

    backup_file = base_file + ".bak";
    if (file_size(backup_file) > 0)
        return backup_file;

    temp_file = base_file + ".tmp";
    if (file_size(temp_file) > 0)
        return temp_file;

    return "";
}

private int remove_character_save_variant(string file)
{
    if (!stringp(file) || file == "" || file_size(file) < 0)
        return 0;

    return rm(file) ? 1 : 0;
}

private int character_save_residue_exists(string account_id)
{
    string base_file;
    string suffix;

    if (!stringp(account_id) || account_id == "")
        return 1;

    base_file = DATA_DIR + "user/" + account_id[0..0] + "/" + account_id + ".o";
    foreach (suffix in ({ "", ".gz", ".tmp", ".bak" })) {
        if (file_size(base_file + suffix) >= 0)
            return 1;
    }

    return 0;
}

private int purge_character_runtime_data(string account_id)
{
    int purged;

    if (!stringp(account_id) || account_id == "")
        return 0;

    if (catch(purged += call_other(QUEST_D, "purge_player_quest_data", account_id)))
        log_file("system/char_delete", sprintf("quest purge failed account=%s\n", account_id));
    if (catch(purged += call_other(WORLD_STATE_D, "purge_player_scopes", account_id)))
        log_file("system/char_delete", sprintf("world state purge failed account=%s\n", account_id));
    return purged;
}

/**
 * @brief 删除角色
 * @param account_id 账号ID
 * @param char_name 角色名
 * @return 1成功，0失败
 */
int delete_character(string account_id, string char_name)
{
    string file;
    int removed;
    int had_account_link;
    string *chars;

    if (!account_id || !char_name)
        return 0;

    if (call_other(PLAYER_D, "consume_registration_test_fault",
        account_id, "character_delete"))
        return 0;

    file = DATA_DIR + "user/" + account_id[0..0] + "/" + account_id + ".o";
    chars = call_other(PLAYER_D, "query_account_chars", account_id);
    had_account_link = arrayp(chars) && member_array(account_id, chars) != -1;

    removed += remove_character_save_variant(file);
    removed += remove_character_save_variant(file + ".gz");
    removed += remove_character_save_variant(file + ".bak");
    removed += remove_character_save_variant(file + ".tmp");

    call_other(PLAYER_D, "remove_character_from_account", account_id, account_id);
    removed += purge_character_runtime_data(account_id);

    return removed > 0 || had_account_link;
}

/**
 * @brief 获取账号角色ID列表（兼容入口）
 * @param account_id 账号ID
 * @return 角色ID数组
 */
string *query_account_chars(string account_id)
{
    return call_other(PLAYER_D, "query_account_chars", account_id);
}

/**
 * @brief 检查角色是否存在
 * @param account_id 账号ID
 * @return 1存在，0不存在
 */
int character_exists(string account_id)
{
    if (!account_id)
        return 0;

    return character_save_residue_exists(account_id);
}

/**
 * @brief 获取角色数量
 * @param account_id 账号ID
 * @return 角色数量
 */
int query_char_count(string account_id)
{
    string *chars;

    chars = query_account_chars(account_id);

    return sizeof(chars);
}

// 初始化
void create()
{
    ::create();
}
