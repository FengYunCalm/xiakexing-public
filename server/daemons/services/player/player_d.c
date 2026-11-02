/**
 * @file    player_d.c
 * @brief   玩家服务真源



 */

#pragma strict_types

#include <system/globals.h>
#include <system/account.h>
#include <system/services.h>

inherit "/lib/framework/daemon";

#define LOGIN_SESSION "/kernel/login/login_session"
#define CHAR_D "/daemons/game/char_d"
#define REGISTRATION_TEST_COMMAND "/cmds/admin/registrationcheck"
#define REGISTRATION_TEST_ACCOUNT_PREFIX "loadtest_regfault_"

string *query_account_chars(string username);

private nosave object _config_service;
private nosave mapping _pending_account_registrations = ([]);
private nosave mapping _committed_account_registrations = ([]);
private nosave mapping _registration_test_faults = ([]);
private nosave int _next_account_registration_token = 0;

private mapping begin_login_phase_timing()
{
    return call_other(LOGIN_SESSION, "begin_loadtest_login_phase_timing");
}

private int finish_login_phase_timing(string phase, mapping started_at, int result)
{
    call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
        phase, started_at, result);
    return result;
}

private object resolve_config_service()
{
    object config_d;

    if (objectp(_config_service))
        return _config_service;

    config_d = find_object(CONFIG_D);
    if (!objectp(config_d))
        config_d = load_object(CONFIG_D);

    if (objectp(config_d))
        _config_service = config_d;

    return config_d;
}

private mapping query_admin_policy_from_config()
{
    object config_d;
    mixed policy;

    config_d = resolve_config_service();
    if (!objectp(config_d) || !function_exists("query_admin_policy", config_d))
        return ([ "ok": 0, "admins": ({ }), "error": "CONFIG_POLICY_UNAVAILABLE" ]);

    policy = config_d->query_admin_policy();
    if (!mapp(policy) || policy["ok"] != 1 || !arrayp(policy["admins"]))
        return ([ "ok": 0, "admins": ({ }),
            "error": mapp(policy) ? policy["error"] : "CONFIG_POLICY_INVALID" ]);
    return ([ "ok": 1, "admins": policy["admins"] ]);
}

private string *query_admin_ids_from_config()
{
    mapping policy;

    policy = query_admin_policy_from_config();
    return policy["ok"] && arrayp(policy["admins"]) ? policy["admins"] : ({ });
}

private int registration_identity_allowed(string username)
{
    mapping policy;

    policy = query_admin_policy_from_config();
    if (!policy["ok"])
        return 0;
    return member_array(username, policy["admins"]) == -1;
}

private int query_password_min_length_from_config()
{
    object config_d;
    mixed min_len;

    config_d = resolve_config_service();

    if (!objectp(config_d) || !function_exists("query_password_min_len", config_d))
        return ACCOUNT_MIN_PASSWORD_LENGTH;

    min_len = config_d->query_password_min_len();
    return intp(min_len) && min_len > 0 ? min_len : ACCOUNT_MIN_PASSWORD_LENGTH;
}

private string account_name_from_file(string file_name);
private string *list_account_names();

private int is_safe_username(string username)
{
    int i;
    int len;

    if (!stringp(username) || username == "")
        return 0;

    len = strlen(username);
    if (len < ACCOUNT_MIN_USERNAME_LENGTH || len > ACCOUNT_MAX_USERNAME_LENGTH)
        return 0;

    for (i = 0; i < len; i++) {
        int c = username[i];
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_')) {
            return 0;
        }
    }

    return 1;
}

private int is_safe_password(string password)
{
    int len;
    int min_len;

    if (!stringp(password) || password == "")
        return 0;

    len = strlen(password);
    min_len = query_password_min_length_from_config();
    if (len < min_len)
        return 0;

    /* 驱动 strlen 按字符计数，而落盘 hash 受 bcrypt 的 72 字节上限约束：
     * 上限按 UTF-8 字节判定，避免多字节密码绕过字节边界。 */
    if (call_other(PASSWORD_HASH_D, "query_utf8_byte_length", password)
        > ACCOUNT_MAX_PASSWORD_LENGTH)
        return 0;

    return 1;
}

int validate_password_format(string password)
{
    return is_safe_password(password);
}

private int is_registration_test_username(string username)
{
    return is_safe_username(username)
        && strsrch(username, REGISTRATION_TEST_ACCOUNT_PREFIX) == 0;
}

private string *registration_test_fault_stages()
{
    return ({
        "character_save_before",
        "character_delete",
        "account_commit_before",
        "account_tmp_write",
        "account_rename_initial",
        "account_rename_final",
        "account_backup_restore",
        "account_delete",
        "session_create",
    });
}

private int is_registration_test_command(object caller)
{
    return objectp(caller) && base_name(caller) == REGISTRATION_TEST_COMMAND;
}

private int is_registration_test_fault_consumer(object caller)
{
    string caller_path;

    if (!objectp(caller))
        return 0;
    caller_path = base_name(caller);
    return member_array(caller_path, ({
        REGISTRATION_TEST_COMMAND,
        CHAR_D,
        LOGIN_SESSION,
    })) >= 0;
}

private int consume_armed_registration_test_fault(string username, string stage)
{
    mapping faults;
    int remaining;

    faults = _registration_test_faults[username];
    if (!mapp(faults) || !intp(faults[stage]) || faults[stage] <= 0)
        return 0;

    remaining = faults[stage] - 1;
    if (remaining > 0)
        faults[stage] = remaining;
    else
        map_delete(faults, stage);
    if (sizeof(keys(faults)) == 0)
        map_delete(_registration_test_faults, username);
    return 1;
}

int arm_registration_test_fault(string username, string stage)
{
    mapping faults;

    if (!is_registration_test_command(previous_object())
        || !is_registration_test_username(username)
        || member_array(stage, registration_test_fault_stages()) < 0)
        return 0;

    faults = _registration_test_faults[username];
    if (!mapp(faults))
        faults = ([]);
    faults[stage] = (faults[stage] || 0) + 1;
    _registration_test_faults[username] = faults;
    return 1;
}

int consume_registration_test_fault(string username, string stage)
{
    if (!is_registration_test_fault_consumer(previous_object())
        || !is_registration_test_username(username)
        || member_array(stage, registration_test_fault_stages()) < 0)
        return 0;

    return consume_armed_registration_test_fault(username, stage);
}

int clear_registration_test_faults(string username)
{
    if (!is_registration_test_command(previous_object())
        || !is_registration_test_username(username))
        return 0;

    map_delete(_registration_test_faults, username);
    return 1;
}

private string build_safe_account_path(string username)
{
    if (!is_safe_username(username))
        return "";

    return sprintf("%s%c/%s", ACCOUNT_DIR, username[0], username);
}

/* L-59：主文件在两次 rename 之间会短暂缺失（甚至因中断长期缺失）。
 * 这是可恢复状态，不能当成“没有这个账号”：优先用备份提回主文件，
 * 只有“主文件缺失且无备份”时才把 tmp 当成唯一候选（初次落盘中断）。
 * 备份与临时文件都不是第二个账号，恢复后主文件唯一。 */
private string *_account_recovery_log;

private int recover_account_file(string username)
{
    string file;
    string candidate;

    if (!stringp(username) || username == "")
        return 0;
    file = build_safe_account_path(username);
    if (file == "")
        return 0;
    if (file_size(file + ".o") >= 0)
        return 1;

    candidate = file_size(file + ".o.bak") >= 0 ? file + ".o.bak"
        : file_size(file + ".o.tmp") >= 0 ? file + ".o.tmp" : "";
    if (candidate == "")
        return 0;
    if (rename(candidate, file + ".o") != 0)
        return 0;

    if (!arrayp(_account_recovery_log))
        _account_recovery_log = ({ });
    _account_recovery_log += ({ sprintf("%s %s -> .o", ctime(time()), candidate) });
    if (sizeof(_account_recovery_log) > 200)
        _account_recovery_log = _account_recovery_log[sizeof(_account_recovery_log) - 200 ..];
    log_file("account", sprintf("%s recovered interrupted account file for %s from %s\n",
        ctime(time()), username, candidate));
    return 1;
}

/* 供诊断/测试观察恢复动作（只返回摘要，不含账号数据）。 */
string *query_account_recovery_log()
{
    return arrayp(_account_recovery_log) ? _account_recovery_log + ({ }) : ({ });
}

private mapping load_account_data(string username)
{
    string file;
    string content;
    mapping data;
    mapping started_at;

    started_at = begin_login_phase_timing();

    file = build_safe_account_path(username);
    if (file == "") {
        finish_login_phase_timing("account_read", started_at, 0);
        return 0;
    }

    if (file_size(file + ".o") < 0)
        recover_account_file(username);

    content = read_file(file + ".o");
    if (!stringp(content) || content == "") {
        finish_login_phase_timing("account_read", started_at, 0);
        return 0;
    }

    data = restore_variable(content);
    finish_login_phase_timing("account_read", started_at, mapp(data));
    return mapp(data) ? data : 0;
}

private int save_account_data(string username, mapping data)
{
    string file;
    string dir;
    string tmp_file;
    string backup_file;
    string payload;
    mapping started_at;
    int renamed;
    int restored;

    if (!is_safe_username(username) || !mapp(data))
        return 0;

    file = build_safe_account_path(username);
    if (file == "")
        return 0;

    dir = sprintf("%s%c", ACCOUNT_DIR, username[0]);
    if (file_size(ACCOUNT_DIR) == -1)
        mkdir(ACCOUNT_DIR);
    if (file_size(dir) == -1)
        mkdir(dir);

    started_at = begin_login_phase_timing();
    payload = save_variable(data);
    if (!stringp(payload) || payload == "") {
        finish_login_phase_timing("account_serialize", started_at, 0);
        return 0;
    }
    finish_login_phase_timing("account_serialize", started_at, 1);

    tmp_file = file + ".o.tmp";
    backup_file = file + ".o.bak";
    started_at = begin_login_phase_timing();
    rm(tmp_file);
    if (consume_armed_registration_test_fault(username, "account_tmp_write")
        || !write_file(tmp_file, payload, 1)) {
        finish_login_phase_timing("account_tmp_write", started_at, 0);
        return 0;
    }
    finish_login_phase_timing("account_tmp_write", started_at, 1);

    if (file_size(file + ".o") < 0) {
        started_at = begin_login_phase_timing();
        renamed = !consume_armed_registration_test_fault(
            username, "account_rename_initial")
            && rename(tmp_file, file + ".o") == 0;
        finish_login_phase_timing("account_rename", started_at, renamed);
        if (!renamed) {
            rm(tmp_file);
            return 0;
        }

        return 1;
    }

    started_at = begin_login_phase_timing();
    rm(backup_file);
    finish_login_phase_timing("account_backup_cleanup", started_at,
        file_size(backup_file) < 0);

    started_at = begin_login_phase_timing();
    renamed = rename(file + ".o", backup_file) == 0;
    finish_login_phase_timing("account_rename", started_at, renamed);
    if (!renamed) {
        rm(tmp_file);
        return 0;
    }

    started_at = begin_login_phase_timing();
    renamed = !consume_armed_registration_test_fault(
        username, "account_rename_final")
        && rename(tmp_file, file + ".o") == 0;
    finish_login_phase_timing("account_rename", started_at, renamed);
    if (!renamed) {
        started_at = begin_login_phase_timing();
        restored = !consume_armed_registration_test_fault(
            username, "account_backup_restore")
            && rename(backup_file, file + ".o") == 0;
        finish_login_phase_timing("account_backup_restore", started_at,
            restored);
        if (!restored)
            return 0;

        rm(tmp_file);
        return 0;
    }

    started_at = begin_login_phase_timing();
    rm(backup_file);
    finish_login_phase_timing("account_backup_cleanup", started_at,
        file_size(backup_file) < 0);

    return 1;
}

private string hash_password(string password, string salt)
{
    mixed result;

    if (!stringp(password) || password == "")
        return "";

    result = call_other(PASSWORD_HASH_D, "hash_password", password);
    return stringp(result) ? result : "";
}

int account_exists(string username)
{
    mapping started_at;
    int exists;

    started_at = begin_login_phase_timing();
    exists = mapp(load_account_data(username));
    return finish_login_phase_timing("account_existence", started_at, exists);
}

int account_has_character(string username)
{
    return sizeof(query_account_chars(username)) > 0;
}

private string generate_account_registration_token()
{
    _next_account_registration_token++;
    return sprintf("%d:%d:%d", time(), perf_counter_ns(),
        _next_account_registration_token);
}

private int query_new_account_registration_residue(string username)
{
    string file;

    file = build_safe_account_path(username);
    if (file == "")
        return 1;

    return file_size(file + ".o") >= 0
        || file_size(file + ".o.gz") >= 0
        || file_size(file + ".o.tmp") >= 0
        || file_size(file + ".o.bak") >= 0;
}

private int remove_new_account_registration_files(string username)
{
    string file;
    string *variants;
    string variant;
    int removed;

    file = build_safe_account_path(username);
    if (file == "")
        return 0;

    variants = ({
        file + ".o",
        file + ".o.gz",
        file + ".o.tmp",
        file + ".o.bak",
    });
    foreach (variant in variants) {
        if (file_size(variant) < 0)
            continue;
        if (consume_armed_registration_test_fault(username, "account_delete"))
            return 0;
        if (!rm(variant))
            return 0;
    }

    removed = !query_new_account_registration_residue(username);
    return removed;
}

mapping query_registration_test_account_state(string username)
{
    string file;
    mapping data;
    mapping faults;
    string stored_hash;
    string hash_kind;
    int fault_count;
    int count;

    if (!is_registration_test_command(previous_object())
        || !is_registration_test_username(username))
        return 0;

    file = build_safe_account_path(username);
    data = file_size(file + ".o") >= 0 ? load_account_data(username) : 0;
    faults = _registration_test_faults[username];
    fault_count = 0;
    if (mapp(faults)) {
        foreach (string stage, count in faults)
            fault_count += count;
    }
    stored_hash = mapp(data) && stringp(data["password_hash"])
        ? data["password_hash"] : "";
    hash_kind = call_other(PASSWORD_HASH_D, "query_hash_kind", stored_hash);

    return ([
        "account_main": file_size(file + ".o") >= 0,
        "account_gzip": file_size(file + ".o.gz") >= 0,
        "account_tmp": file_size(file + ".o.tmp") >= 0,
        "account_backup": file_size(file + ".o.bak") >= 0,
        "pending": mapp(_pending_account_registrations[username]),
        "committed": mapp(_committed_account_registrations[username]),
        "faults": fault_count,
        "character_links": mapp(data) && arrayp(data["characters"])
            ? sizeof(data["characters"]) : 0,
        "hash_kind": hash_kind,
    ]);
}

int cleanup_registration_test_account(string username)
{
    if (!is_registration_test_command(previous_object())
        || !is_registration_test_username(username))
        return 0;

    map_delete(_registration_test_faults, username);
    map_delete(_pending_account_registrations, username);
    map_delete(_committed_account_registrations, username);
    remove_new_account_registration_files(username);
    return !query_new_account_registration_residue(username);
}

int create_registration_test_account(string username, string password,
    int with_character_link)
{
    mapping data;
    string password_hash;

    if (!is_registration_test_command(previous_object())
        || !is_registration_test_username(username)
        || !is_safe_password(password))
        return 0;

    map_delete(_registration_test_faults, username);
    map_delete(_pending_account_registrations, username);
    map_delete(_committed_account_registrations, username);
    remove_new_account_registration_files(username);
    if (query_new_account_registration_residue(username))
        return 0;

    password_hash = hash_password(password, "");
    if (!stringp(password_hash) || password_hash == "")
        return 0;

    data = ([
        "username": username,
        "password_hash": password_hash,
        "created": time(),
        "last_login": 0,
        "characters": with_character_link ? ({ username }) : ({ }),
    ]);
    return save_account_data(username, data);
}

mapping query_account_registration_transaction_status()
{
    return ([
        "pending": sizeof(keys(_pending_account_registrations)),
        "committed": sizeof(keys(_committed_account_registrations)),
    ]);
}

mapping prepare_new_account_registration(string username, string password)
{
    mapping started_at;
    mapping hash_started_at;
    mapping registration;
    string password_hash;
    string token;

    started_at = begin_login_phase_timing();
    if (!is_safe_username(username) || !registration_identity_allowed(username)
        || !is_safe_password(password)
        || mapp(_pending_account_registrations[username])
        || mapp(_committed_account_registrations[username])
        || account_exists(username)
        || query_new_account_registration_residue(username)) {
        finish_login_phase_timing("account_prepare", started_at, 0);
        return 0;
    }

    hash_started_at = begin_login_phase_timing();
    password_hash = hash_password(password, "");
    finish_login_phase_timing("account_hash", hash_started_at,
        stringp(password_hash) && password_hash != "");
    if (!stringp(password_hash) || password_hash == "") {
        finish_login_phase_timing("account_prepare", started_at, 0);
        return 0;
    }

    token = generate_account_registration_token();
    registration = ([
        "token": token,
        "password_hash": password_hash,
        "created": time(),
        "commit_attempted": 0,
    ]);
    _pending_account_registrations[username] = registration;
    finish_login_phase_timing("account_prepare", started_at, 1);

    return ([ "token": token ]);
}

int commit_new_account_registration(string username, string token,
    string char_id, string char_name)
{
    mapping started_at;
    mapping registration;
    mapping data;
    int saved;

    started_at = begin_login_phase_timing();
    registration = _pending_account_registrations[username];
    if (!is_safe_username(username) || !registration_identity_allowed(username)
        || !mapp(registration)
        || !stringp(token) || token == "" || registration["token"] != token
        || !stringp(char_id) || char_id == ""
        || !stringp(char_name) || char_name == ""
        || account_exists(username)
        || query_new_account_registration_residue(username)) {
        return finish_login_phase_timing("account_final_commit", started_at, 0);
    }

    if (consume_armed_registration_test_fault(username, "account_commit_before"))
        return finish_login_phase_timing("account_final_commit", started_at, 0);

    data = ([
        "username": username,
        "password_hash": registration["password_hash"],
        "created": registration["created"],
        "last_login": time(),
        "characters": ({ char_id })
    ]);
    registration["commit_attempted"] = 1;
    saved = save_account_data(username, data);
    if (!saved)
        return finish_login_phase_timing("account_final_commit", started_at, 0);

    map_delete(_pending_account_registrations, username);
    _committed_account_registrations[username] = ([
        "token": token,
        "commit_attempted": 1,
    ]);
    return finish_login_phase_timing("account_final_commit", started_at, 1);
}

int confirm_new_account_registration(string username, string token)
{
    mapping registration;

    registration = _committed_account_registrations[username];
    if (!is_safe_username(username) || !mapp(registration)
        || !stringp(token) || registration["token"] != token) {
        return 0;
    }

    map_delete(_committed_account_registrations, username);
    return 1;
}

int rollback_new_account_registration(string username, string token)
{
    mapping started_at;
    mapping registration;
    int removed;
    int residue;

    started_at = begin_login_phase_timing();
    registration = _pending_account_registrations[username];
    if (!mapp(registration))
        registration = _committed_account_registrations[username];
    if (!is_safe_username(username) || !mapp(registration)
        || !stringp(token) || registration["token"] != token) {
        return finish_login_phase_timing("registration_rollback", started_at, 0);
    }

    if (registration["commit_attempted"]) {
        removed = remove_new_account_registration_files(username);
        residue = query_new_account_registration_residue(username);
    } else {
        removed = 1;
        residue = 0;
    }
    if (!removed || residue)
        return finish_login_phase_timing("registration_rollback", started_at, 0);

    map_delete(_pending_account_registrations, username);
    map_delete(_committed_account_registrations, username);
    return finish_login_phase_timing("registration_rollback", started_at, 1);
}

int create_account(string username, string password)
{
    mapping data;
    mapping started_at;
    mapping hash_started_at;
    string password_hash;
    int saved;

    started_at = begin_login_phase_timing();

    /*
     * 账号服务是真正落盘密码哈希的地方，因此长度边界必须在这里兜住，
     * 不能只依赖登录输入层的前置校验。
     */
    if (!is_safe_username(username) || !registration_identity_allowed(username)
        || !is_safe_password(password)
        || mapp(_pending_account_registrations[username])
        || mapp(_committed_account_registrations[username])
        || account_exists(username)
        || query_new_account_registration_residue(username))
        return finish_login_phase_timing("account_prepare", started_at, 0);

    hash_started_at = begin_login_phase_timing();
    password_hash = hash_password(password, "");
    finish_login_phase_timing("account_hash", hash_started_at,
        stringp(password_hash) && password_hash != "");
    // hash 失败必须在持久化前终止：空 hash 账号不能落盘成“创建成功”。
    if (!stringp(password_hash) || password_hash == "") {
        log_file("system/account_create", sprintf("hash failed username=%s\n", username));
        return finish_login_phase_timing("account_prepare", started_at, 0);
    }

    data = ([
        "username": username,
        "password_hash": password_hash,
        "created": time(),
        "last_login": 0,
        "characters": ({})
    ]);

    saved = save_account_data(username, data);
    if (!saved) {
        log_file("system/account_create", sprintf("save failed username=%s\n", username));
        return finish_login_phase_timing("account_prepare", started_at, 0);
    }

    return finish_login_phase_timing("account_prepare", started_at, 1);
}

int validate_password(string username, string password)
{
    mapping data;
    string stored_hash;
    int valid;

    if (!is_safe_username(username) || !is_safe_password(password))
        return 0;

    data = load_account_data(username);
    if (!mapp(data))
        return 0;

    stored_hash = data["password_hash"];
    if (!stringp(stored_hash) || stored_hash == "")
        return 0;

    valid = call_other(PASSWORD_HASH_D, "verify_password", password,
        stored_hash);
    if (!valid)
        return 0;

    return 1;
}

string *query_admin_ids()
{
    return query_admin_ids_from_config();
}

int is_admin_account(string username)
{
    if (!is_safe_username(username))
        return 0;

    return member_array(username, query_admin_ids_from_config()) != -1;
}

void update_login_time(string username)
{
    mapping data;

    data = load_account_data(username);
    if (!mapp(data))
        return;

    data["last_login"] = time();
    save_account_data(username, data);
}

int add_character_to_account(string username, string char_id, string char_name)
{
    mapping data;
    string *chars;

    /*
     * 账号真源里只持有“可持久化定位角色存档”的 character id。
     * 当前单角色模型下，这个 id 与 account_id 一致；char_name 仅保留给旧调用签名。
     */
    if (!is_safe_username(username) || !stringp(char_id) || char_id == "")
        return 0;

    data = load_account_data(username);
    if (!mapp(data))
        return 0;

    chars = data["characters"];
    if (!arrayp(chars))
        chars = ({});

    if (member_array(char_id, chars) == -1)
        chars += ({ char_id });

    data["characters"] = chars;
    return save_account_data(username, data);
}

int finalize_character_registration(string username, string char_id, string char_name)
{
    mapping data;
    mapping started_at;
    string *chars;
    int saved;

    started_at = begin_login_phase_timing();

    /*
     * 新角色建立后，角色索引与本次登录时间属于同一个账号状态提交。
     * 合并为一次原子替换，避免 300 人注册时对同一账号文件连续读写两遍。
     * char_name 保留给现有调用签名；账号真源仍只持久化稳定 character id。
     */
    if (!is_safe_username(username) || !stringp(char_id) || char_id == "")
        return finish_login_phase_timing("account_final_commit", started_at, 0);

    data = load_account_data(username);
    if (!mapp(data))
        return finish_login_phase_timing("account_final_commit", started_at, 0);

    chars = data["characters"];
    if (!arrayp(chars))
        chars = ({ });
    if (member_array(char_id, chars) == -1)
        chars += ({ char_id });

    data["characters"] = chars;
    data["last_login"] = time();
    saved = save_account_data(username, data);
    return finish_login_phase_timing("account_final_commit", started_at, saved);
}

/**
 * @brief 查询账号关联的角色ID列表
 * @param username 账号ID
 * @return 角色ID数组
 */
string *query_account_chars(string username)
{
    mapping data;
    mixed chars;

    if (!is_safe_username(username))
        return ({ });

    data = load_account_data(username);
    if (!mapp(data))
        return ({ });

    chars = data["characters"];
    return arrayp(chars) ? chars + ({ }) : ({ });
}

string *query_all_account_ids()
{
    return list_account_names();
}

/* L-61：账号主文件只能靠严格后缀识别。
 * sscanf("%s.o") 会把 alice.o.bak / alice.o.tmp / alice.o.gz 也解析成 alice，
 * 导致备份与临时文件被当成第二个账号参与枚举、删除和迁移。 */
private string account_name_from_file(string file_name)
{
    int length;

    if (!stringp(file_name) || file_name == "")
        return 0;
    length = strlen(file_name);
    if (length <= 3)
        return 0;
    if (file_name[length - 2 .. length - 1] != ".o")
        return 0;
    return file_name[0 .. length - 3];
}

private string *list_account_names()
{
    string *dirs;
    string *files;
    string *names;
    string dir_name;
    string file_name;
    string username;

    dirs = get_dir(ACCOUNT_DIR);
    if (!arrayp(dirs))
        return ({ });

    names = ({ });
    foreach (dir_name in dirs) {
        if (!stringp(dir_name) || dir_name == "." || dir_name == "..")
            continue;
        if (file_size(ACCOUNT_DIR + dir_name) != -2)
            continue;
        files = get_dir(ACCOUNT_DIR + dir_name + "/");
        if (!arrayp(files))
            continue;
        foreach (file_name in files) {
            username = account_name_from_file(file_name);
            if (!stringp(username) || !is_safe_username(username))
                continue;
            if (member_array(username, names) == -1)
                names += ({ username });
        }
    }
    return names;
}

/**
 * @brief 从账号移除角色ID
 * @param username 账号ID
 * @param char_id 角色ID
 * @return 1成功，0失败
 */
int remove_character_from_account(string username, string char_id)
{
    mapping data;
    string *chars;

    if (!is_safe_username(username) || !stringp(char_id) || char_id == "")
        return 0;

    data = load_account_data(username);
    if (!mapp(data))
        return 0;

    chars = data["characters"];
    if (!arrayp(chars))
        chars = ({ });

    if (member_array(char_id, chars) == -1)
        return 1;

    chars -= ({ char_id });
    data["characters"] = chars;
    return save_account_data(username, data);
}

string *query_recent_account_ids(int within_seconds)
{
    string *dirs;
    string *files;
    string *account_ids;
    string dir_name;
    string file_name;
    string username;
    mapping data;
    int cutoff;

    if (!intp(within_seconds) || within_seconds <= 0)
        return ({ });

    dirs = get_dir(ACCOUNT_DIR);
    if (!arrayp(dirs))
        return ({ });

    account_ids = ({ });
    cutoff = time() - within_seconds;

    foreach (dir_name in dirs) {
        if (!stringp(dir_name) || dir_name == "." || dir_name == "..")
            continue;
        if (file_size(ACCOUNT_DIR + dir_name) != -2)
            continue;

        files = get_dir(ACCOUNT_DIR + dir_name + "/");
        if (!arrayp(files))
            continue;

        foreach (file_name in files) {
            username = account_name_from_file(file_name);
            if (!stringp(username) || !is_safe_username(username))
                continue;

            data = load_account_data(username);
            if (!mapp(data))
                continue;
            if ((data["last_login"] || 0) < cutoff)
                continue;

            account_ids += ({ username });
        }
    }

    return account_ids;
}

void create()
{
    ::create();
    _pending_account_registrations = ([]);
    _committed_account_registrations = ([]);
    _registration_test_faults = ([]);
    _next_account_registration_token = 0;
}
