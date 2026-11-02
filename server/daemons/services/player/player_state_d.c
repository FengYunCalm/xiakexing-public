/**
 * @file    player_state_d.c
 * @brief   玩家持久状态的唯一业务写入口。
 *
 * 该服务负责路径白名单、schema 版本、generation/CAS、深拷贝候选和
 * 失败回滚。player/base 对外仍保留原有 set/query facade，但持久状态
 * 不再直接暴露内部 mapping。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

#undef is_player

inherit "/lib/framework/daemon";

#define PLAYER_STATE_SCHEMA_VERSION 2
#define PLAYER_STATE_OWNER_PREFIX   "player/"
#define PLAYER_STATE_SCOPE          "player_state"
#define MAX_OWNER_PROPOSALS        8192

nosave mapping _owner_proposals;
nosave int _owner_proposal_sequence;

private string *managed_prefixes()
{
    return ({
        "id", "name", "title", "gender", "age", "race", "level",
        "combat", "vitals", "hp", "mp", "energy", "potential",
        "exp", "combat_exp", "skills", "skill", "skill_breakthrough",
        "advancement",
        "growth", "meridian", "economy", "mail", "inventory", "equipment",
        "life_skills", "life_skills_exp", "sect", "guild", "guild_skill",
        "quest",
        "quests", "escort", "rank", "reputation", "attributes",
        "stats", "login", "last_login", "last_on", "created_at", "location",
        "birth_place", "practice", "cultivation", "honor", "contribution",
        "persistence", "owner", "world", "reward", "exploration", "idle",
        "listen", "family", "money", "moral", "neili", "qi", "jing",
        "player",
        "max_qi", "max_jing", "str", "dex", "con", "int", "per", "spi",
        "kar", "eff_qi", "eff_jing", "skill_exp", "skill_unlocks",
        "loadtest_fixture", "account_id", "owner_id", "npc_favor",
        "guild_rank", "guild_contribution", "guild_salary_last", "rank_claim_last",
        "balance", "realm", "migration", "combatcheck", "dungeon",
        "instance", "instance_id", "schedule", "acquisition", "provenance",
        "xk",
    });
}

private int path_has_prefix(string path, string prefix)
{
    return path == prefix
        || (strlen(path) > strlen(prefix)
            && path[0..strlen(prefix) - 1] == prefix
            && path[strlen(prefix)] == '/');
}

private int is_managed_path(string path)
{
    string prefix;

    if (!stringp(path) || path == "")
        return 0;
    foreach (prefix in managed_prefixes()) {
        if (path_has_prefix(path, prefix))
            return 1;
    }
    return 0;
}

private string owner_id(object player);
private mapping mutate_state_fields(object player, mapping changes,
    string *deletes, mapping context);

private int is_player_target(object player)
{
    return objectp(player) && function_exists("is_player", player)
        && player->is_player();
}

private mapping result(int ok, string code, string message)
{
    return ([
        "ok": ok ? 1 : 0,
        "code": stringp(code) ? code : (ok ? "" : "PLAYER_STATE_FAILED"),
        "message": stringp(message) ? message : "",
        "scope": PLAYER_STATE_SCOPE,
    ]);
}

private string owner_id(object player)
{
    mixed raw_id;

    raw_id = is_player_target(player)
        ? player->data_owner_query("id") : "";
    if (stringp(raw_id) && raw_id != "")
        return raw_id;
    if (arrayp(raw_id) && sizeof(raw_id) > 0 && stringp(raw_id[0])
        && raw_id[0] != "")
        return raw_id[0];
    return "unknown";
}

private int query_generation(object player)
{
    mixed value;

    if (!is_player_target(player))
        return 0;
    value = player->query_temp("player_state/generation");
    if (intp(value) && value >= 0)
        return value;
    value = player->data_owner_query("persistence/player_state_generation");
    if (!intp(value) || value < 0)
        value = 0;
    player->set_temp("player_state/generation", value);
    return value;
}

private void bump_generation(object player)
{
    int next_generation;

    if (!is_player_target(player))
        return;
    next_generation = query_generation(player) + 1;
    player->data_owner_set("persistence/player_state_generation",
        next_generation);
    player->set_temp("player_state/generation", next_generation);
}

private void publish_generation(object player)
{
    if (is_player_target(player))
        catch(OWNER_COMMIT_D->set_owner_generation(
            "owner/player/" + owner_id(player), query_generation(player)));
}

private mapping copy_mapping(mapping value)
{
    mixed copied;
    mixed err;

    if (!mapp(value))
        return 0;
    err = catch(copied = restore_variable(save_variable(value)));
    return !err && mapp(copied) ? copied : 0;
}

private int serialized_values_equal(mixed left, mixed right)
{
    mixed left_copy;
    mixed right_copy;
    mixed err;

    err = catch(left_copy = save_variable(left));
    if (err)
        return 0;
    err = catch(right_copy = save_variable(right));
    return !err && left_copy == right_copy;
}

#define RESTORE_IDENTITY_PATHS      ({ "id", "account_id", "owner_id", "name", \
    "player/id", "player/account_id", "player/owner_id", "player/name" })

/* 身份保护按结构化路径判断，而不是只匹配字面字段名：
 * - 路径本身是受保护路径；
 * - 路径是受保护路径的祖先（替换父 mapping 会覆盖受保护子字段）；
 * - 路径是受保护路径的后代（子字段同属身份）。
 * 否则一次父路径整体替换就能绕过身份保护。 */
private int is_restore_identity_path(string path)
{
    string protected_path;

    if (!stringp(path) || path == "")
        return 0;

    foreach (protected_path in RESTORE_IDENTITY_PATHS) {
        if (path == protected_path)
            return 1;
        if (strsrch(protected_path, path + "/") == 0)
            return 1;
        if (strsrch(path, protected_path + "/") == 0)
            return 1;
    }

    return 0;
}

private int valid_string_array(mixed value)
{
    mixed item;

    if (!arrayp(value))
        return 0;
    foreach (item in value)
        if (!stringp(item))
            return 0;
    return 1;
}

private int valid_mapping_field(mapping data, string field)
{
    mixed value;

    value = data[field];
    return undefinedp(value) || mapp(value);
}

private int valid_string_field(mapping data, string field)
{
    mixed value;

    value = data[field];
    return undefinedp(value) || stringp(value);
}

private int snapshot_is_valid(mapping data)
{
    mapping persistence;
    mixed schema_version;
    mixed id_value;

    if (!mapp(data))
        return 0;
    persistence = data["persistence"];
    if (!undefinedp(persistence) && !mapp(persistence))
        return 0;
    if (mapp(persistence)) {
        schema_version = persistence["schema_version"];
        if (!undefinedp(schema_version) && !intp(schema_version))
            return 0;
        if (intp(schema_version) && schema_version > PLAYER_STATE_SCHEMA_VERSION)
            return 0;
    }

    id_value = data["id"];
    if (!undefinedp(id_value) && !stringp(id_value)
        && !valid_string_array(id_value))
        return 0;
    foreach (string field in ({
        "name", "title", "gender", "race", "birth_place", "account_id",
        "owner_id"
    })) {
        if (!valid_string_field(data, field))
            return 0;
    }
    foreach (string field in ({
        "combat", "vitals", "skills", "skill", "advancement", "growth",
        "meridian", "economy", "inventory", "equipment", "life_skills",
        "life_skills_exp", "guild_skill", "quest", "quests", "escort",
        "reputation", "attributes",
        "stats", "login", "persistence", "owner", "world", "reward",
        "exploration", "idle", "listen", "family", "dungeon", "instance",
        "schedule", "acquisition", "provenance", "xk", "rank_claim_last",
        "realm", "migration", "combatcheck"
    })) {
        if (!valid_mapping_field(data, field))
            return 0;
    }
    return 1;
}

private mapping normalize_snapshot(mapping data)
{
    mapping normalized;
    mapping persistence;

    normalized = copy_mapping(data);
    if (!mapp(normalized))
        return 0;
    persistence = mapp(normalized["persistence"])
        ? normalized["persistence"] : ([]);
    persistence["schema_version"] = PLAYER_STATE_SCHEMA_VERSION;
    normalized["persistence"] = persistence;
    return normalized;
}

private mapping query_snapshot(object player)
{
    mapping snapshot;

    if (!is_player_target(player))
        return 0;
    snapshot = player->data_owner_query_data();
    return copy_mapping(snapshot);
}

private int expected_generation_matches(object player, mapping context,
    mapping response)
{
    int expected;
    int actual;

    if (!mapp(context) || undefinedp(context["expected_generation"]))
        return 1;
    expected = context["expected_generation"];
    if (!intp(expected)) {
        response["code"] = "PLAYER_STATE_INVALID_GENERATION";
        response["message"] = "expected_generation must be int";
        return 0;
    }
    actual = query_generation(player);
    if (expected != actual) {
        response["code"] = "PLAYER_STATE_STALE_GENERATION";
        response["message"] = "player state generation is stale";
        response["expected_generation"] = expected;
        response["actual_generation"] = actual;
        response["stale"] = 1;
        return 0;
    }
    return 1;
}

private mapping mutation_result(mapping response, int generation)
{
    response["generation"] = generation;
    response["schema_version"] = PLAYER_STATE_SCHEMA_VERSION;
    response["owner"] = PLAYER_STATE_OWNER_PREFIX + response["player_id"];
    return response;
}

private mapping set_state_internal(object player, string path, mixed value,
    mapping context)
{
    mapping response;
    mapping before;
    int generation;
    string player_id;

    response = result(0, "", "");
    if (!is_player_target(player))
        return result(0, "PLAYER_STATE_INVALID_TARGET", "target is not a player");
    if (!is_managed_path(path))
        return result(0, "PLAYER_STATE_PATH_DENIED", "state path is not managed");

    player_id = owner_id(player);
    response["player_id"] = player_id;
    generation = query_generation(player);
    if (!expected_generation_matches(player, context, response))
        return mutation_result(response, generation);

    before = query_snapshot(player);
    if (!mapp(before))
        return mutation_result(result(0, "PLAYER_STATE_SNAPSHOT_FAILED",
            "player state snapshot failed") + ([ "player_id": player_id ]),
            generation);
    if (!player->data_owner_set(path, value)) {
        response["code"] = "PLAYER_STATE_WRITE_FAILED";
        response["message"] = "state value rejected";
        return mutation_result(response, generation);
    }
    if (!snapshot_is_valid(player->data_owner_query_data())) {
        player->data_owner_replace(before);
        response["code"] = "PLAYER_STATE_SCHEMA_FAILED";
        response["message"] = "state schema rejected mutation";
        return mutation_result(response, generation);
    }

    bump_generation(player);
    publish_generation(player);
    response["ok"] = 1;
    response["code"] = "PLAYER_STATE_COMMITTED";
    response["message"] = "";
    response["path"] = path;
    response["player_id"] = player_id;
    response["applied"] = 1;
    return mutation_result(response, query_generation(player));
}

private mapping delete_state_internal(object player, string path, mapping context)
{
    mapping response;
    mapping before;
    int generation;
    string player_id;

    response = result(0, "", "");
    if (!is_player_target(player))
        return result(0, "PLAYER_STATE_INVALID_TARGET", "target is not a player");
    if (!is_managed_path(path))
        return result(0, "PLAYER_STATE_PATH_DENIED", "state path is not managed");

    player_id = owner_id(player);
    response["player_id"] = player_id;
    generation = query_generation(player);
    if (!expected_generation_matches(player, context, response))
        return mutation_result(response, generation);

    before = query_snapshot(player);
    if (!mapp(before))
        return mutation_result(result(0, "PLAYER_STATE_SNAPSHOT_FAILED",
            "player state snapshot failed") + ([ "player_id": player_id ]),
            generation);
    if (!player->data_owner_delete(path)) {
        response["code"] = "PLAYER_STATE_DELETE_FAILED";
        response["message"] = "state path delete failed";
        return mutation_result(response, generation);
    }
    if (!snapshot_is_valid(player->data_owner_query_data())) {
        player->data_owner_replace(before);
        response["code"] = "PLAYER_STATE_SCHEMA_FAILED";
        response["message"] = "state schema rejected deletion";
        return mutation_result(response, generation);
    }

    bump_generation(player);
    publish_generation(player);
    response["ok"] = 1;
    response["code"] = "PLAYER_STATE_COMMITTED";
    response["path"] = path;
    response["player_id"] = player_id;
    response["applied"] = 1;
    return mutation_result(response, query_generation(player));
}

private mapping replace_state_internal(object player, mapping data,
    mapping context)
{
    mapping response;
    mapping before;
    mapping candidate;
    int generation;
    string player_id;

    response = result(0, "", "");
    if (!is_player_target(player))
        return result(0, "PLAYER_STATE_INVALID_TARGET", "target is not a player");
    if (!mapp(data) || !snapshot_is_valid(data))
        return result(0, "PLAYER_STATE_SCHEMA_FAILED", "invalid player state snapshot");

    player_id = owner_id(player);
    generation = query_generation(player);
    response["player_id"] = player_id;
    if (!expected_generation_matches(player, context, response))
        return mutation_result(response, generation);

    candidate = normalize_snapshot(data);
    before = query_snapshot(player);
    if (!mapp(candidate) || !mapp(before))
        return mutation_result(result(0, "PLAYER_STATE_SNAPSHOT_FAILED",
            "player state snapshot failed") + ([ "player_id": player_id ]),
            generation);
    if (!player->data_owner_replace(candidate))
        return mutation_result(result(0, "PLAYER_STATE_WRITE_FAILED",
            "player state snapshot rejected") + ([ "player_id": player_id ]),
            generation);
    if (!snapshot_is_valid(player->data_owner_query_data())) {
        player->data_owner_replace(before);
        return mutation_result(result(0, "PLAYER_STATE_SCHEMA_FAILED",
            "player state schema rejected snapshot") + ([ "player_id": player_id ]),
            generation);
    }

    bump_generation(player);
    publish_generation(player);
    response["ok"] = 1;
    response["code"] = "PLAYER_STATE_COMMITTED";
    response["message"] = "";
    response["player_id"] = player_id;
    response["applied"] = 1;
    response["replace"] = 1;
    return mutation_result(response, query_generation(player));
}

mapping query_state(object player, string path);

string query_player_id(object player)
{
    return objectp(player) ? get_object_id(player) : "";
}

string build_state_idempotency_key(object player, string operation,
    mixed mutation)
{
    mapping state;
    string player_id;
    int generation;

    if (!is_player_target(player))
        return "";
    player_id = query_player_id(player);
    if (!stringp(player_id) || player_id == "")
        return "";
    state = query_state(player, "persistence/player_state_generation");
    generation = mapp(state) && intp(state["generation"])
        ? state["generation"] : 0;
    return sprintf("player/service/%s/%s/%d/%s", operation || "mutation",
        player_id, generation, hash("sha256", sprintf("%O", mutation)));
}

mapping query_state(object player, string path)
{
    mixed value;

    if (!is_player_target(player) || !is_managed_path(path))
        return 0;
    value = player->data_owner_query(path);
    return ([
        "ok": 1,
        "path": path,
        "value": value,
        "generation": query_generation(player),
        "schema_version": PLAYER_STATE_SCHEMA_VERSION,
    ]);
}

mixed query_state_value(object player, string path)
{
    mapping response;

    response = query_state(player, path);
    return mapp(response) ? response["value"] : 0;
}

/* Owner rollback must distinguish an absent path from a persisted integer 0.
 * The check stays behind PLAYER_STATE_D so callers never receive raw player
 * data or bypass the managed-path policy. */
int query_state_path_exists(object player, string path)
{
    mapping data;
    mixed cursor;
    string *parts;
    string part;

    if (!is_player_target(player) || !is_managed_path(path))
        return 0;
    data = player->data_owner_query_data();
    if (!mapp(data))
        return 0;
    cursor = data;
    parts = explode(path, "/");
    foreach (part in parts) {
        if (!mapp(cursor) || undefinedp(cursor[part]))
            return 0;
        cursor = cursor[part];
    }
    return 1;
}

mapping set_state(object player, string path, mixed value, mapping context)
{
    return set_state_internal(player, path, value,
        mapp(context) ? context : ([]));
}

mapping add_state(object player, string path, int delta, mapping context)
{
    mixed current;

    if (!is_player_target(player) || !is_managed_path(path))
        return result(0, "PLAYER_STATE_PATH_DENIED", "state path is not managed");
    current = player->data_owner_query(path);
    if (!intp(current))
        current = 0;
    return set_state_internal(player, path, current + delta,
        mapp(context) ? context : ([]));
}

mapping delete_state(object player, string path, mapping context)
{
    return delete_state_internal(player, path,
        mapp(context) ? context : ([]));
}

mapping replace_state_data(object player, mapping data, mapping context)
{
    return replace_state_internal(player, data,
        mapp(context) ? context : ([]));
}

/*
 * Restore only the top-level fields changed by a failed durable operation.
 * The comparison is performed against the current generation and the
 * resulting batch uses the normal owner mutation path; it must never replace
 * a newer player snapshot wholesale.
 */
mapping restore_state_snapshot_if_generation(object player, mapping before_data,
    int expected_generation, string operation_id)
{
    mapping current;
    mapping changes;
    mapping response;
    string *deletes;
    string key;
    string changed_path;
    string deleted_path;
    int actual_generation;

    response = result(0, "", "");
    if (!is_player_target(player) || !mapp(before_data)
        || !snapshot_is_valid(before_data))
        return result(0, "PLAYER_STATE_RESTORE_SNAPSHOT_INVALID",
            "restore snapshot is invalid");
    if (!intp(expected_generation) || expected_generation < 0)
        return result(0, "PLAYER_STATE_INVALID_GENERATION",
            "restore expected_generation must be int");

    actual_generation = query_generation(player);
    response["player_id"] = owner_id(player);
    response["expected_generation"] = expected_generation;
    response["actual_generation"] = actual_generation;
    if (actual_generation != expected_generation) {
        response["code"] = "PLAYER_STATE_RESTORE_STALE_GENERATION";
        response["message"] = "restore generation is stale";
        response["stale"] = 1;
        return mutation_result(response, actual_generation);
    }

    current = query_snapshot(player);
    changes = ([ ]);
    deletes = ({ });
    if (!mapp(current))
        return mutation_result(result(0, "PLAYER_STATE_SNAPSHOT_FAILED",
            "current player state snapshot failed") +
            ([ "player_id": owner_id(player) ]), actual_generation);

    foreach (key in keys(before_data)) {
        if (!is_managed_path(key))
            return mutation_result(result(0, "PLAYER_STATE_PATH_DENIED",
                "restore snapshot contains an unmanaged path") +
                ([ "player_id": owner_id(player), "path": key ]),
                actual_generation);
        if (is_restore_identity_path(key)
            && (undefinedp(current[key])
                || !serialized_values_equal(before_data[key], current[key])))
            return mutation_result(result(0, "PLAYER_STATE_IDENTITY_CONFLICT",
                "restore cannot change player identity") +
                ([ "player_id": owner_id(player), "path": key ]),
                actual_generation);
        if (undefinedp(current[key])) {
            // 失败的操作删掉了 before 里存在的字段：恢复必须把它写回。
            // 原实现把这种情况放进 deletes，等于对已缺失的字段再删一次，
            // 结果“恢复”后字段仍然缺失（方向反了）。
            if (!is_restore_identity_path(key))
                changes[key] = before_data[key];
        } else if (!serialized_values_equal(before_data[key], current[key])
            && !is_restore_identity_path(key))
            changes[key] = before_data[key];
    }
    foreach (key in keys(current)) {
        if (!is_managed_path(key))
            return mutation_result(result(0, "PLAYER_STATE_PATH_DENIED",
                "current snapshot contains an unmanaged path") +
                ([ "player_id": owner_id(player), "path": key ]),
                actual_generation);
        if (undefinedp(before_data[key])) {
            if (is_restore_identity_path(key))
                return mutation_result(result(0, "PLAYER_STATE_IDENTITY_CONFLICT",
                    "restore cannot change player identity") +
                    ([ "player_id": owner_id(player), "path": key ]),
                    actual_generation);
            deletes += ({ key });
        }
    }

    // 恢复前后粒度必须自洽：同一次恢复里既设置又删除存在祖先/后代关系的路径，
    // 会先写回再被父路径删除，等于静默丢掉恢复结果。这里 fail-closed。
    foreach (changed_path in keys(changes)) {
        foreach (deleted_path in deletes) {
            if (changed_path == deleted_path
                || strsrch(changed_path, deleted_path + "/") == 0
                || strsrch(deleted_path, changed_path + "/") == 0)
                return mutation_result(result(0, "PLAYER_STATE_MUTATION_CONFLICT",
                    "restore would set and delete overlapping paths") +
                    ([ "player_id": owner_id(player), "path": changed_path,
                        "conflict_path": deleted_path ]), actual_generation);
        }
    }

    if (sizeof(changes) == 0 && sizeof(deletes) == 0) {
        response["ok"] = 1;
        response["code"] = "PLAYER_STATE_RESTORE_NOOP";
        response["operation_id"] = stringp(operation_id) ? operation_id : "";
        response["restored"] = 0;
        return mutation_result(response, actual_generation);
    }

    response = mutate_state_fields(player, changes, deletes, ([
        "expected_generation": expected_generation,
    ]));
    response["operation_id"] = stringp(operation_id) ? operation_id : "";
    if (response["ok"])
        response["restored"] = 1;
    return response;
}

private mapping mutate_state_fields(object player, mapping changes,
    string *deletes, mapping context)
{
    mapping response;
    mapping before;
    string path;
    mixed value;
    int generation;
    int applied;
    int deleted;

    response = result(0, "", "");
    if (!is_player_target(player))
        return result(0, "PLAYER_STATE_INVALID_TARGET", "target is not a player");
    if (!mapp(changes))
        changes = ([]);
    if (!arrayp(deletes))
        deletes = ({ });
    if (sizeof(changes) == 0 && sizeof(deletes) == 0)
        return result(0, "PLAYER_STATE_EMPTY_BATCH", "state mutation set is empty");

    generation = query_generation(player);
    response["player_id"] = owner_id(player);
    if (!expected_generation_matches(player, context, response))
        return mutation_result(response, generation);
    foreach (path, value in changes) {
        if (!is_managed_path(path))
            return mutation_result(result(0, "PLAYER_STATE_PATH_DENIED",
                "state path is not managed") + ([ "player_id": owner_id(player) ]),
                generation);
    }
    foreach (path in deletes) {
        if (!is_managed_path(path))
            return mutation_result(result(0, "PLAYER_STATE_PATH_DENIED",
                "state path is not managed") + ([ "player_id": owner_id(player) ]),
                generation);
        if (!undefinedp(changes[path]))
            return mutation_result(result(0, "PLAYER_STATE_MUTATION_CONFLICT",
                "state path cannot be changed and deleted together") +
                ([ "player_id": owner_id(player), "path": path ]), generation);
    }

    before = query_snapshot(player);
    if (!mapp(before))
        return mutation_result(result(0, "PLAYER_STATE_SNAPSHOT_FAILED",
            "player state snapshot failed") + ([ "player_id": owner_id(player) ]),
            generation);

    foreach (path, value in changes) {
        if (!player->data_owner_set(path, value)) {
            player->data_owner_replace(before);
            return mutation_result(result(0, "PLAYER_STATE_WRITE_FAILED",
                "state batch rejected") + ([ "player_id": owner_id(player),
                    "failed_path": path ]), generation);
        }
        applied++;
    }
    foreach (path in deletes) {
        if (!player->data_owner_delete(path)) {
            player->data_owner_replace(before);
            return mutation_result(result(0, "PLAYER_STATE_DELETE_FAILED",
                "state batch deletion rejected") + ([ "player_id": owner_id(player),
                    "failed_path": path ]), generation);
        }
        deleted++;
    }
    if (!snapshot_is_valid(player->data_owner_query_data())) {
        player->data_owner_replace(before);
        return mutation_result(result(0, "PLAYER_STATE_SCHEMA_FAILED",
            "state schema rejected batch") + ([ "player_id": owner_id(player) ]),
            generation);
    }

    bump_generation(player);
    publish_generation(player);
    response["ok"] = 1;
    response["code"] = "PLAYER_STATE_COMMITTED";
    response["message"] = "";
    response["applied"] = applied;
    response["deleted"] = deleted;
    response["paths"] = keys(changes);
    response["delete_paths"] = deletes + ({ });
    return mutation_result(response, query_generation(player));
}

mapping set_state_fields(object player, mapping changes, mapping context)
{
    return mutate_state_fields(player, changes, ({ }),
        mapp(context) ? context : ([]));
}

mapping delete_state_fields(object player, string *paths, mapping context)
{
    return mutate_state_fields(player, ([]), paths,
        mapp(context) ? context : ([]));
}

private int is_owner_mutation_path(string path)
{
    /* Stable identity and the generation watermark are owned by the state
     * service itself, not by an arbitrary cross-owner proposal.
     * 边界：初始化/离线路径（char_d 建档、player_init 兜底、migration 离线对象）
     * 通过 facade 的本地提交（set_state/delete_state，条件为 interactive 且
     * state_owner_ready 的玩家之外）写 id；在线 owner proposal 一律拒绝改身份。
     * 这保证 id 在角色进入世界后不可被跨 owner 提案重写。 */
    return is_managed_path(path) && path != "id"
        && path != "persistence/player_state_generation";
}

private object query_online_player_by_id(string player_id)
{
    object candidate;

    if (!stringp(player_id) || player_id == "")
        return 0;
    foreach (candidate in users()) {
        if (!is_player_target(candidate))
            continue;
        if (get_object_id(candidate) == player_id)
            return candidate;
    }
    return 0;
}

private int query_owner_expected_generation(mapping txn, string owner)
{
    mapping expected;
    mixed value;

    if (!mapp(txn) || !stringp(owner) || owner == "")
        return -1;
    expected = txn["expected_generations"];
    if (!mapp(expected) || undefinedp(expected[owner]))
        return -1;
    value = expected[owner];
    return intp(value) && value >= 0 ? value : -1;
}

private int is_ownercheck_probe(mapping payload)
{
    return mapp(payload) && stringp(payload["ownercheck"])
        && payload["ownercheck"] != "";
}

/* 命令边界事务（compact command）的 payload 只有 verb/args/actor 等
 * 命令上下文，不含状态变更：真实领域提交在命令体内各自的 owner 提交
 * 里完成。这类事务只做提交边界，handler 不得把它当作空变更拒绝。 */
private int is_command_boundary_payload(mapping payload)
{
    return mapp(payload) && stringp(payload["verb"]) && payload["verb"] != ""
        && stringp(payload["operation_id"]) && payload["operation_id"] != "";
}

private void prune_owner_proposals()
{
    string key;
    string oldest_key;
    int oldest_sequence;
    mapping proposal;

    if (!mapp(_owner_proposals) || sizeof(_owner_proposals) <= MAX_OWNER_PROPOSALS)
        return;
    oldest_key = "";
    oldest_sequence = 0;
    foreach (key, proposal in _owner_proposals) {
        if (!mapp(proposal)) {
            oldest_key = key;
            break;
        }
        if (oldest_key == "" || proposal["sequence"] < oldest_sequence) {
            oldest_key = key;
            oldest_sequence = proposal["sequence"];
        }
    }
    if (stringp(oldest_key) && oldest_key != "")
        map_delete(_owner_proposals, oldest_key);
}

mapping prepare_owner_proposal(mapping txn)
{
    mapping payload;
    mapping changes;
    mapping data;
    mapping before;
    mapping existing;
    mapping proposal;
    string *deletes;
    string player_id;
    string owner;
    string expected_owner;
    string path;
    string delete_path;
    string idempotency_key;
    mixed value;
    int generation;
    int has_changes;
    int has_deletes;
    int has_replace;

    if (!mapp(txn) || txn["domain"] != "player")
        return ([ "ok": 0, "reason": "player transaction is required" ]);
    payload = txn["payload"];
    if (!mapp(payload))
        return ([ "ok": 0, "reason": "player proposal payload is required" ]);
    changes = mapp(payload["changes"])
        ? copy_mapping(payload["changes"]) : ([]);
    deletes = arrayp(payload["deletes"])
        ? payload["deletes"] + ({ }) : ({ });
    has_replace = !undefinedp(payload["data"]);
    if (has_replace && !mapp(payload["data"]))
        return ([ "ok": 0, "reason": "player replacement data is invalid" ]);
    data = has_replace ? copy_mapping(payload["data"]) : 0;
    has_changes = mapp(changes) && sizeof(changes) > 0;
    has_deletes = sizeof(deletes) > 0;
    if (has_replace && (has_changes || has_deletes))
        return ([ "ok": 0, "reason": "player proposal mutations are mixed" ]);
    if (!has_replace && !has_changes && !has_deletes) {
        if (is_ownercheck_probe(payload) || is_command_boundary_payload(payload)
            || (mapp(txn) && txn["compact_command"]))
            return ([ "ok": 1, "prepared": 1, "skipped": 1,
                "boundary_only": 1, "real_apply": 0,
                "direct_cross_owner_write": 0 ]);
        return ([ "ok": 0, "reason": "player proposal mutation is required" ]);
    }
    player_id = payload["player_id"];
    if (!stringp(player_id) || player_id == "")
        return ([ "ok": 0, "reason": "player proposal id is required" ]);
    owner = "owner/player/" + player_id;
    if (!arrayp(txn["owners"]) || sizeof(txn["owners"]) == 0
        || txn["owners"][0] != owner)
        return ([ "ok": 0, "reason": "player proposal owner mismatch" ]);
    expected_owner = owner;
    generation = query_owner_expected_generation(txn, expected_owner);
    if (generation < 0)
        return ([ "ok": 0, "reason": "player proposal generation is required" ]);
    foreach (path, value in changes) {
        if (!stringp(path) || !is_owner_mutation_path(path))
            return ([ "ok": 0, "reason": "player proposal path is denied",
                "path": path ]);
    }
    foreach (delete_path in deletes) {
        if (!stringp(delete_path) || !is_owner_mutation_path(delete_path))
            return ([ "ok": 0, "reason": "player proposal delete path is denied",
                "path": delete_path ]);
    }
    if (has_replace && !snapshot_is_valid(data))
        return ([ "ok": 0, "reason": "player replacement schema is invalid" ]);
    idempotency_key = txn["idempotency_key"];
    if (!stringp(idempotency_key) || idempotency_key == "")
        return ([ "ok": 0, "reason": "player proposal idempotency key is required" ]);
    existing = _owner_proposals[idempotency_key];
    if (mapp(existing)) {
        if (existing["player_id"] != player_id
            || sprintf("%O", existing["changes"]) != sprintf("%O", changes)
            || sprintf("%O", existing["deletes"]) != sprintf("%O", deletes)
            || sprintf("%O", existing["data"]) != sprintf("%O", data)
            || existing["generation"] != generation)
            return ([ "ok": 0, "reason": "player proposal idempotency conflict",
                "error_code": "PLAYER_PROPOSAL_IDEMPOTENCY_CONFLICT" ]);
        return ([ "ok": 1, "prepared": 1, "duplicate": 1,
            "real_apply": 1, "generation": existing["generation"],
            "direct_cross_owner_write": 0 ]);
    }

    before = query_snapshot(query_online_player_by_id(player_id));
    if (!mapp(before))
        return ([ "ok": 0, "reason": "player owner is not online" ]);
    if (query_generation(query_online_player_by_id(player_id)) != generation)
        return ([ "ok": 0, "reason": "player owner generation is stale",
            "error_code": "PLAYER_STATE_STALE_GENERATION", "stale": 1 ]);
    proposal = ([
        "sequence": ++_owner_proposal_sequence,
        "player_id": player_id,
        "generation": generation,
        "changes": changes,
        "deletes": deletes,
        "data": data,
        "before": before,
        "applied": 0,
        "created_at": time(),
    ]);
    _owner_proposals[idempotency_key] = proposal;
    prune_owner_proposals();
    return ([ "ok": 1, "prepared": 1, "real_apply": 1,
        "generation": generation, "direct_cross_owner_write": 0 ]);
}

mapping apply_owner_proposal(mapping txn)
{
    mapping payload;
    mapping proposal;
    mapping after;
    mapping response;
    object player;
    string player_id;
    string idempotency_key;
    int generation;

    if (!mapp(txn) || txn["domain"] != "player")
        return ([ "ok": 0, "reason": "player transaction is required" ]);
    payload = txn["payload"];
    if ((is_ownercheck_probe(payload) || is_command_boundary_payload(payload)
            || (mapp(txn) && txn["compact_command"]))
        && !mapp(payload["changes"]) && !arrayp(payload["deletes"])
        && undefinedp(payload["data"]))
        return ([ "ok": 1, "applied": 1, "skipped": 1, "boundary_only": 1,
            "real_apply": 0, "direct_cross_owner_write": 0 ]);
    idempotency_key = txn["idempotency_key"];
    proposal = _owner_proposals[idempotency_key];
    if (!mapp(proposal))
        return ([ "ok": 0, "reason": "player proposal was not prepared" ]);
    player_id = proposal["player_id"];
    player = query_online_player_by_id(player_id);
    if (!objectp(player))
        return ([ "ok": 0, "reason": "player owner is not online" ]);
    if (proposal["applied"]) {
        generation = query_generation(player);
        if (generation == proposal["after_generation"])
            return ([ "ok": 1, "applied": 1, "duplicate": 1,
                "generation": generation, "direct_cross_owner_write": 0 ]);
        return ([ "ok": 0, "reason": "player proposal was already applied",
            "error_code": "PLAYER_PROPOSAL_APPLIED_CONFLICT" ]);
    }
    generation = query_generation(player);
    if (generation != proposal["generation"])
        return ([ "ok": 0, "reason": "player owner generation changed",
            "error_code": "PLAYER_STATE_STALE_GENERATION", "stale": 1,
            "expected_generation": proposal["generation"],
            "actual_generation": generation ]);
    if (mapp(proposal["data"]))
        response = replace_state_internal(player, proposal["data"], ([
            "expected_generation": proposal["generation"],
        ]));
    else if (mapp(proposal["changes"]) && sizeof(proposal["changes"]) > 0
        && arrayp(proposal["deletes"]) && sizeof(proposal["deletes"]) > 0)
        response = mutate_state_fields(player, proposal["changes"],
            proposal["deletes"], ([
                "expected_generation": proposal["generation"],
            ]));
    else if (mapp(proposal["changes"]) && sizeof(proposal["changes"]) > 0)
        response = set_state_fields(player, proposal["changes"], ([
            "expected_generation": proposal["generation"],
        ]));
    else
        response = delete_state_fields(player, proposal["deletes"], ([
            "expected_generation": proposal["generation"],
        ]));
    if (!mapp(response) || response["ok"] != 1)
        return ([ "ok": 0, "reason": "player proposal apply rejected",
            "handler_result": response ]);
    after = query_snapshot(player);
    if (!mapp(after))
        return ([ "ok": 0, "reason": "player proposal after snapshot failed" ]);
    proposal["after"] = after;
    proposal["after_generation"] = query_generation(player);
    proposal["applied"] = 1;
    proposal["applied_at"] = time();
    _owner_proposals[idempotency_key] = proposal;
    return ([ "ok": 1, "applied": 1,
        "generation": proposal["after_generation"],
        "direct_cross_owner_write": 0 ]);
}

mapping rollback_owner_proposal(mapping txn)
{
    mapping payload;
    mapping proposal;
    mapping current;
    object player;
    string idempotency_key;
    int generation;
    int before_generation;
    int after_generation;

    if (!mapp(txn) || txn["domain"] != "player")
        return ([ "ok": 0, "reason": "player transaction is required" ]);
    payload = txn["payload"];
    if (is_ownercheck_probe(payload) && !mapp(payload["changes"])
        && !arrayp(payload["deletes"]))
        return ([ "ok": 1, "rolled_back": 1, "skipped": 1,
            "real_apply": 0, "direct_cross_owner_write": 0 ]);
    idempotency_key = txn["idempotency_key"];
    proposal = _owner_proposals[idempotency_key];
    if (!mapp(proposal))
        return ([ "ok": 1, "rolled_back": 1, "skipped": 1,
            "direct_cross_owner_write": 0 ]);
    if (!proposal["applied"])
        return ([ "ok": 1, "rolled_back": 1, "skipped": 1,
            "direct_cross_owner_write": 0 ]);
    player = query_online_player_by_id(proposal["player_id"]);
    if (!objectp(player))
        return ([ "ok": 0, "reason": "player owner is not online",
            "compensation_required": 1 ]);
    generation = query_generation(player);
    before_generation = proposal["generation"];
    after_generation = proposal["after_generation"];
    current = query_snapshot(player);
    if (generation == before_generation && mapp(current)
        && sprintf("%O", current) == sprintf("%O", proposal["before"])) {
        proposal["applied"] = 0;
        proposal["rolled_back"] = 1;
        _owner_proposals[idempotency_key] = proposal;
        return ([ "ok": 1, "rolled_back": 1, "duplicate": 1,
            "generation": before_generation, "direct_cross_owner_write": 0 ]);
    }
    if (generation != after_generation)
        return ([ "ok": 0, "reason": "player owner generation changed before rollback",
            "error_code": "PLAYER_PROPOSAL_ROLLBACK_STALE", "stale": 1,
            "expected_generation": after_generation,
            "actual_generation": generation, "compensation_required": 1 ]);
    if (!player->data_owner_replace(proposal["before"])
        || !snapshot_is_valid(player->data_owner_query_data())
        || !player->data_owner_set("persistence/player_state_generation",
            before_generation)) {
        return ([ "ok": 0, "reason": "player proposal rollback rejected",
            "compensation_required": 1 ]);
    }
    player->set_temp("player_state/generation", before_generation);
    publish_generation(player);
    proposal["applied"] = 0;
    proposal["rolled_back"] = 1;
    proposal["rolled_back_at"] = time();
    _owner_proposals[idempotency_key] = proposal;
    return ([ "ok": 1, "rolled_back": 1,
        "generation": before_generation, "direct_cross_owner_write": 0 ]);
}

private int owner_transaction_ready(object player)
{
    return is_player_target(player) && interactive(player)
        && player->query_temp("player/state_owner_ready");
}

private mapping commit_owner_state_proposal(object player, mapping payload,
    string idempotency_key)
{
    mapping created;
    mapping prepared;
    mapping committed;
    mapping finalized;
    mapping proposal;
    string player_id;
    string player_owner;
    int generation;
    int watermark;
    int txn_id;

    if (!is_player_target(player) || !mapp(payload))
        return result(0, "PLAYER_STATE_INVALID_PROPOSAL", "player and payload are required");
    if (!stringp(idempotency_key) || idempotency_key == "")
        return result(0, "PLAYER_STATE_IDEMPOTENCY_REQUIRED", "idempotency key is required");
    player_id = owner_id(player);
    if (!stringp(player_id) || player_id == "" || player_id == "unknown")
        return result(0, "PLAYER_STATE_IDENTITY_REQUIRED", "player identity is required");
    player_owner = "owner/player/" + player_id;
    /* 跨会话 generation 对齐：协调器水位是本 owner 的单调权威
     * （set_owner_generation 只增不减）。非正常断线（destruct 未 save）
     * 会让磁盘恢复的 generation 落后于水位；此时必须采纳水位，否则
     * 重登后第一次 owner 提交永远 OWNER_GENERATION_STALE，账号在
     * driver 重启前无法再次登录。 */
    watermark = OWNER_COMMIT_D->query_owner_generation(player_owner);
    if (watermark > query_generation(player)) {
        player->data_owner_set("persistence/player_state_generation", watermark);
        player->set_temp("player_state/generation", watermark);
    }
    generation = query_generation(player);
    publish_generation(player);
    proposal = copy_mapping(payload);
    if (!mapp(proposal))
        return result(0, "PLAYER_STATE_PROPOSAL_COPY_FAILED", "player proposal copy failed");
    proposal["player_id"] = player_id;
    proposal["expected_generation"] = generation;
    proposal["expected_generations"] = ([ player_owner: generation ]);
    proposal["schema_version"] = PLAYER_STATE_SCHEMA_VERSION;
    proposal["command_id"] = idempotency_key;
    proposal["causation_id"] = "player_state/commit";
    created = OWNER_COMMIT_D->create_domain_transaction("player", idempotency_key,
        ({ player_owner }), proposal);
    if (!mapp(created) || created["ok"] != 1)
        return result(0, "PLAYER_STATE_TRANSACTION_CREATE_FAILED", "player transaction create failed")
            + ([ "transaction": created ]);
    if (created["duplicate"] && mapp(created["transaction"])
        && member_array(created["transaction"]["state"],
            ({ "committed", "finalized" })) != -1)
        return ([ "ok": 1, "duplicate": 1,
            "transaction": created["transaction"], "player_id": player_id,
            "generation": query_generation(player) ]);
    txn_id = mapp(created["transaction"]) ? created["transaction"]["id"] : 0;
    prepared = OWNER_COMMIT_D->prepare_transaction(txn_id);
    if (!mapp(prepared) || prepared["ok"] != 1)
        return result(0, "PLAYER_STATE_TRANSACTION_PREPARE_FAILED", "player transaction prepare failed")
            + ([ "transaction": prepared ]);
    committed = OWNER_COMMIT_D->commit_transaction(txn_id);
    if (!mapp(committed) || committed["ok"] != 1)
        return result(0, "PLAYER_STATE_TRANSACTION_COMMIT_FAILED", "player transaction commit failed")
            + ([ "transaction": committed ]);
    finalized = OWNER_COMMIT_D->finalize_transaction(txn_id);
    if (!mapp(finalized) || finalized["ok"] != 1)
        return result(0, "PLAYER_STATE_TRANSACTION_FINALIZE_FAILED", "player transaction finalize failed")
            + ([ "transaction": finalized ]);
    return ([ "ok": 1, "code": "PLAYER_STATE_TRANSACTION_COMMITTED",
        "player_id": player_id, "generation": query_generation(player),
        "transaction": finalized, "direct_cross_owner_write": 0 ]);
}

mapping commit_state_fields(object player, mapping changes, string idempotency_key)
{
    mapping payload;

    if (!is_player_target(player) || !mapp(changes) || sizeof(changes) == 0)
        return result(0, "PLAYER_STATE_INVALID_PROPOSAL", "player and changes are required");
    foreach (string path in keys(changes))
        if (!is_owner_mutation_path(path))
            return result(0, "PLAYER_STATE_PATH_DENIED", "state path is not managed");
    if (!owner_transaction_ready(player))
        return set_state_fields(player, changes, ([ ]));
    payload = ([
        "mutation": "set_fields",
        "changes": copy_mapping(changes),
    ]);
    return commit_owner_state_proposal(player, payload, idempotency_key);
}

mapping commit_state_deletions(object player, string *paths, string idempotency_key)
{
    mapping payload;

    if (!is_player_target(player) || !arrayp(paths) || sizeof(paths) == 0)
        return result(0, "PLAYER_STATE_INVALID_PROPOSAL", "player and paths are required");
    foreach (string path in paths)
        if (!is_owner_mutation_path(path))
            return result(0, "PLAYER_STATE_PATH_DENIED", "state path is not managed");
    if (!owner_transaction_ready(player))
        return delete_state_fields(player, paths, ([ ]));
    payload = ([
        "mutation": "delete_fields",
        "deletes": paths + ({ }),
    ]);
    return commit_owner_state_proposal(player, payload, idempotency_key);
}

mapping commit_state_mutations(object player, mapping changes, string *deletes,
    string idempotency_key)
{
    mapping payload;

    if (!is_player_target(player) || !mapp(changes) || !arrayp(deletes)
        || (sizeof(changes) == 0 && sizeof(deletes) == 0))
        return result(0, "PLAYER_STATE_INVALID_PROPOSAL",
            "player and mutations are required");
    foreach (string path in keys(changes))
        if (!is_owner_mutation_path(path))
            return result(0, "PLAYER_STATE_PATH_DENIED", "state path is not managed");
    foreach (string path in deletes)
        if (!is_owner_mutation_path(path))
            return result(0, "PLAYER_STATE_PATH_DENIED", "state path is not managed");
    if (!owner_transaction_ready(player))
        return mutate_state_fields(player, changes, deletes, ([ ]));
    payload = ([
        "mutation": "mutate_fields",
        "changes": copy_mapping(changes),
        "deletes": deletes + ({ }),
    ]);
    return commit_owner_state_proposal(player, payload, idempotency_key);
}

mapping commit_state_data(object player, mapping data, string idempotency_key)
{
    mapping payload;

    if (!is_player_target(player) || !mapp(data))
        return result(0, "PLAYER_STATE_INVALID_PROPOSAL", "player and data are required");
    if (!owner_transaction_ready(player))
        return replace_state_internal(player, data, ([ ]));
    if (!snapshot_is_valid(data))
        return result(0, "PLAYER_STATE_SCHEMA_FAILED", "invalid player state snapshot");
    payload = ([
        "mutation": "replace_data",
        "data": copy_mapping(data),
    ]);
    return commit_owner_state_proposal(player, payload, idempotency_key);
}

int is_managed_state_path(string path)
{
    return is_managed_path(path);
}

mapping query_state_contract()
{
    return ([
        "scope": PLAYER_STATE_SCOPE,
        "schema_version": PLAYER_STATE_SCHEMA_VERSION,
        "generation_key": "persistence/player_state_generation",
        "runtime_generation_key": "player_state/generation",
        "write_entry": "PLAYER_STATE_D",
        "read_entry": "PLAYER_STATE_D",
        "managed_prefixes": managed_prefixes(),
        "cas": 1,
        "deep_copy": 1,
        "atomic_batch": 1,
        "owner_transaction": 1,
        "owner_proposal_mutations": ({ "set_fields", "delete_fields",
            "mutate_fields", "replace_data" }),
        "owner_prepare": "prepare_owner_proposal",
        "owner_apply": "apply_owner_proposal",
        "owner_rollback": "rollback_owner_proposal",
    ]);
}

int validate_state_snapshot(mapping data)
{
    return snapshot_is_valid(data);
}

mapping migrate_state_snapshot(mapping data, int from_version)
{
    if (!mapp(data) || from_version > PLAYER_STATE_SCHEMA_VERSION)
        return 0;
    return normalize_snapshot(data);
}

int query_schema_version()
{
    return PLAYER_STATE_SCHEMA_VERSION;
}

void create()
{
    ::create();
    _owner_proposals = ([]);
    _owner_proposal_sequence = 0;
}
