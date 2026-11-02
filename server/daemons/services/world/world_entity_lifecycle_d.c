/**
 * @file    world_entity_lifecycle_d.c
 * @brief   世界共享实体补刷、索引和租约生命周期 owner。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

/* Entity spawn/index/lease state has one owner: this daemon. */

/* 实体索引 key 的世界键前缀（单属主；调整时全站同步，防 key 分叉）。 */
#define WORLD_KEY_PUBLIC "public"

nosave mapping _room_entities_cache;
nosave mapping _room_entity_object_index;
nosave mapping _entity_object_index;
nosave mapping _lease_token_index;
nosave mapping _lease_slot_index;
nosave string _runtime_epoch;
nosave mapping _respawn_cooldown_seconds;
nosave mapping _respawn_cooldown_until;
nosave mapping _respawn_timers;
nosave int _content_entity_prune_count;

private void remove_content_entity_index(object ob);
private string query_content_entity_index_key_for_ob(object room, object ob);
string query_entity_index_key(object room, string entity_id);
object query_leased_entity_object(string token, string fallback_key);
int ensure_room_entities(object room);

private void schedule_respawn_refresh(object room, string index_key, int deadline)
{
    mapping pending;
    int delay;
    int handle;

    pending = _respawn_timers[index_key];
    if (mapp(pending) && pending["deadline"] == deadline)
        return;
    delay = deadline > time() ? deadline - time() : 0;
    handle = TIMER_D->call_later_walltime_background("refresh_respawn_room",
        delay, this_object(), index_key, deadline);
    if (handle <= 0) {
        log_file("world/respawn", sprintf("respawn timer rejected for %s\n", index_key));
        return;
    }
    if (mapp(pending))
        TIMER_D->cancel_timer(pending["handle"]);
    _respawn_timers[index_key] = ([
        "handle": handle, "deadline": deadline, "room": file_name(room),
    ]);
}

// 定时器只携带稳定键；到期重新读取当前内容，不持有房间或玩家快照。
void refresh_respawn_room(string index_key, int deadline)
{
    mapping pending;
    object room;
    object player;

    pending = _respawn_timers[index_key];
    if (!mapp(pending) || pending["deadline"] != deadline)
        return;
    map_delete(_respawn_timers, index_key);
    if (_respawn_cooldown_until[index_key] != deadline)
        return;
    room = find_object(pending["room"]);
    if (!objectp(room)) {
        map_delete(_respawn_cooldown_until, index_key);
        return;
    }
    if (time() < deadline) {
        schedule_respawn_refresh(room, index_key, deadline);
        return;
    }
    map_delete(_respawn_cooldown_until, index_key);
    room->delete_temp("world_lifecycle/entities_checked_at");
    ROOM_VIEW_D->invalidate_room_view_content(room);
    ensure_room_entities(room);
    foreach (player in all_inventory(room))
        if (interactive(player))
            CLIENT_SYNC_D->request_room_snapshot(player, room, 0);
}

// 拾取前确认实际 spawn 槽；玩家丢回房间的同名物品不能占用新实体的冷却。
string query_pickup_respawn_entity(object room, object item)
{
    string index_key;

    if (!objectp(room) || !objectp(item) || environment(item) != room)
        return "";
    index_key = query_content_entity_index_key_for_ob(room, item);
    return index_key != "" && _room_entity_object_index[index_key] == item
        && _respawn_cooldown_seconds[index_key] > 0 ? index_key : "";
}

void record_item_pickup(object room, string entity_key)
{
    string index_key;
    object indexed;
    int seconds;

    index_key = strsrch(entity_key, WORLD_KEY_PUBLIC + "|") == 0
        ? entity_key : query_entity_index_key(room, entity_key);
    seconds = _respawn_cooldown_seconds[index_key];
    if (index_key == "" || seconds <= 0)
        return;
    indexed = _room_entity_object_index[index_key];
    if (objectp(indexed) && environment(indexed) == room)
        return;
    map_delete(_room_entity_object_index, index_key);
    map_delete(_entity_object_index, index_key);
    _respawn_cooldown_until[index_key] = time() + seconds;
    room->delete_temp("world_lifecycle/entities_checked_at");
    ROOM_VIEW_D->invalidate_room_view_content(room);
    schedule_respawn_refresh(room, index_key, _respawn_cooldown_until[index_key]);
}

mapping query_world_room_owner_route(object room)
{
    mapping route;
    string room_key;
    mixed err;

    if (!objectp(room))
        return ([ "ok": 0, "reason": "room is required" ]);

    room_key = base_name(room);
    err = catch(route = OWNER_RUNTIME_D->query_service_owner_route("world_room", room_key));
    if (err || !mapp(route) || route["ok"] != 1)
        return ([
            "ok": 0,
            "service": "world_room",
            "key": room_key,
            "room_path": room_key,
            "failure_reason": err ? sprintf("owner route query failed: %O", err)
                : (mapp(route) ? route["failure_reason"] : "owner route unavailable"),
            "readiness": mapp(route) ? route["readiness"] : ([ "ok": 0 ]),
            "dependencies": mapp(route) ? route["dependencies"] : ([ ]),
            "direct_cross_owner_write": 0,
        ]);

    route["room_path"] = room_key;
    route["same_owner_direct_only"] = 1;
    route["direct_cross_owner_write"] = 0;
    return route;
}

mapping stamp_world_room_owner_route(object room, string reason)
{
    mapping route;

    if (!objectp(room))
        return ([ "ok": 0, "reason": "room is required" ]);

    route = query_world_room_owner_route(room);
    room->set_temp("owner/world_room_route", route);
    room->set_temp("owner/world_room_route_reason", stringp(reason) ? reason : "");
    return route;
}

private mapping *query_cached_room_entities(string room_path)
{
    mapping *entities;

    if (!stringp(room_path) || room_path == "")
        return ({ });
    if (!mapp(_room_entities_cache))
        _room_entities_cache = ([ ]);
    if (arrayp(_room_entities_cache[room_path]))
        return _room_entities_cache[room_path];

    entities = CONTENT_REGISTRY_D->query_room_entities(room_path);
    _room_entities_cache[room_path] = arrayp(entities) ? entities : ({ });
    return _room_entities_cache[room_path];
}

private string query_content_entity_id(mapping entity)
{
    string entity_id;

    if (!mapp(entity))
        return "";
    entity_id = entity["id"] || entity["present_id"]
        || entity["template_id"] || entity["path"] || "";
    return stringp(entity_id) ? entity_id : "";
}

private string query_content_entity_path(mapping entity)
{
    string path;

    if (!mapp(entity))
        return "";
    path = entity["path"] || "";
    if (!stringp(path) || path == "")
        return "";
    if (strlen(path) > 2 && path[<2..] == ".c")
        path = path[0..<3];
    return path;
}

/* 按全键（world|instance|room|entity_id）解析当前 live clone
 * （唯一公开解析入口；quest 等业务层不得自行组合 file_name/blueprint
 * 猜测）。走写时维护的反向映射（O(1)）；返回前校验 environment 非 0
 * （死亡/被拾取对象即使索引残留也不得当作有效目标）。 */
object query_live_entity_object(string index_key)
{
    object ob;

    if (!stringp(index_key) || index_key == "")
        return 0;
    if (!mapp(_entity_object_index))
        return 0;
    ob = _entity_object_index[index_key];
    if (!objectp(ob) || environment(ob) == 0)
        return 0;
    return ob;
}

/* driver 运行时 epoch——create 时算一次缓存（daemon reload
 * 不变、driver 重启变化）。lease token 携带它，driver 重启后旧 token
 * 明确判 orphan，不得弱绑定。 */
string query_world_runtime_epoch()
{
    return _runtime_epoch;
}

/* 绑定 lease token -> live object（派生索引）。identity 记录
 * 稳定全键（query_entity_index_key 产出），供 reload 后 lazy 回退重建。 */
int bind_entity_lease(string token, object ob, mapping identity)
{
    object bound;
    string entity_key;
    string current_token;

    if (!stringp(token) || token == "" || !objectp(ob))
        return 0;
    if (!mapp(_lease_token_index))
        _lease_token_index = ([ ]);
    if (!mapp(_lease_slot_index))
        _lease_slot_index = ([ ]);
    current_token = ob->query_temp("world_lifecycle/lease_token") || "";
    if (current_token != "" && current_token != token)
        return 0;
    bound = _lease_token_index[token];
    if (objectp(bound) && bound != ob && environment(bound) != 0)
        return 0;
    entity_key = mapp(identity) && stringp(identity["entity_key"])
        ? identity["entity_key"] : "";
    if (entity_key != "") {
        current_token = _lease_slot_index[entity_key];
        if (stringp(current_token) && current_token != ""
            && current_token != token)
            return 0;
        bound = _lease_token_index[token];
        if (objectp(bound) && bound != ob && environment(bound) != 0)
            return 0;
    }
    _lease_token_index[token] = ob;
    if (entity_key != "") {
        ob->set_temp("world_lifecycle/lease_identity_key", entity_key);
        _lease_slot_index[entity_key] = token;
    }
    /* world daemon 自持 capability stamp——补刷门控与
     * fallback 重绑校验只读自己的 key，不跨域解析 escort temp。 */
    ob->set_temp("world_lifecycle/lease_token", token);
    return 1;
}

/* 补刷门控——slot（room + entity_id）当前是否被 escort 租走。
 * 被租则 spawn slot 空置（不删索引不补刷），归还后索引自然重建。
 * 反查 token 后校验 leased clone 仍活，防幽灵占位。 */
int query_slot_leased(object room, string entity_id)
{
    object ob;
    string index_key;
    string token;
    string candidate_key;

    if (!objectp(room) || !stringp(entity_id) || entity_id == "")
        return 0;
    index_key = query_entity_index_key(room, entity_id);
    if (!mapp(_lease_slot_index))
        return 0;
    token = _lease_slot_index[index_key];
    if (stringp(token) && token != "") {
        ob = query_leased_entity_object(token, index_key);
        /* 未能从派生索引恢复也必须 fail closed：slot 仍被 capability 占用，
         * 直到持有 expected object 的 unbind 或明确 discard 清掉。 */
        return 1;
    }
    foreach (candidate_key, token in _lease_slot_index) {
        if (strsrch(candidate_key, index_key + "|slot=") != 0
            || !stringp(token) || token == "")
            continue;
        ob = query_leased_entity_object(token, candidate_key);
        return 1;
    }
    return 0;
}

/* 按 token 解析 leased clone。索引命中直接返回；miss（daemon
 * reload 后索引为空）按 fallback entity_key 回退解析候选，校验候选
 * 携带同 token（capability stamp）后重绑——绝不凭 file_name 弱绑定。
 * 候选环境为 0（已销毁）视为无效。 */
object query_leased_entity_object(string token, string fallback_key)
{
    object ob;

    if (!stringp(token) || token == "")
        return 0;
    if (mapp(_lease_token_index) && objectp(_lease_token_index[token])) {
        ob = _lease_token_index[token];
        if (objectp(ob) && environment(ob) != 0
            && ob->query_temp("world_lifecycle/lease_token") == token)
            return ob;
        map_delete(_lease_token_index, token);
    }
    if (stringp(fallback_key) && fallback_key != "") {
        ob = query_live_entity_object(fallback_key);
        if (objectp(ob)
            && ob->query_temp("world_lifecycle/lease_token") == token) {
            if (!mapp(_lease_token_index))
                _lease_token_index = ([ ]);
            _lease_token_index[token] = ob;
            return ob;
        }
    }
    return 0;
}

/* 解绑 lease。expected 校验防误删他人租约（token 唯一，
 * 但调用方必须持有其解析结果）。 */
int unbind_entity_lease(string token, object expected)
{
    object bound;
    string slot_key;

    if (!stringp(token) || token == "" || !objectp(expected))
        return 0;
    if (!mapp(_lease_token_index))
        _lease_token_index = ([ ]);
    bound = _lease_token_index[token];
    if (!objectp(bound)
        && expected->query_temp("world_lifecycle/lease_token") == token)
        bound = expected;
    if (!objectp(bound)
        && expected->query_temp("world_lifecycle/lease_token") != token) {
        /* reload 后双方派生索引都可能为空；若没有任何 slot 仍持有该
         * token，重复解绑是幂等成功。若 slot 仍被占用则拒绝猜测。 */
        if (mapp(_lease_slot_index) && sizeof(_lease_slot_index) > 0)
            return 0;
        return 1;
    }
    if (bound != expected
        || expected->query_temp("world_lifecycle/lease_token") != token)
        return 0;
    slot_key = bound->query_temp("world_lifecycle/lease_identity_key");
    if (mapp(_lease_slot_index) && stringp(slot_key)
        && slot_key != "" && _lease_slot_index[slot_key] == token)
        map_delete(_lease_slot_index, slot_key);
    map_delete(_lease_token_index, token);
    expected->delete_temp("world_lifecycle/lease_token");
    expected->delete_temp("world_lifecycle/lease_identity_key");
    return 1;
}

/* 清理已销毁 clone 的派生 lease。调用方必须先用持久 owner 记录确认
 * token 与 slot_key 属于同一 lease；这里仅清理 daemon 自有索引。 */
int discard_entity_lease(string token, string slot_key)
{
    object bound;

    if (!stringp(token) || token == "")
        return 0;
    bound = mapp(_lease_token_index) ? _lease_token_index[token] : 0;
    if (mapp(_lease_slot_index) && stringp(slot_key)
        && slot_key != "" && _lease_slot_index[slot_key] == token)
        map_delete(_lease_slot_index, slot_key);
    if (mapp(_lease_token_index))
        map_delete(_lease_token_index, token);
    if (objectp(bound)
        && bound->query_temp("world_lifecycle/lease_token") == token) {
        bound->delete_temp("world_lifecycle/lease_token");
        bound->delete_temp("world_lifecycle/lease_identity_key");
    }
    return 1;
}

/* 公开全键构造（与正向索引同构）：room + entity_id -> 全键。
 * 业务层（escort lease 等）用它记录稳定身份，再经 query_live_entity_object
 * 解析；裸 entity_id 跨房间不唯一（同 id 多房间各 spawn 一个）。 */
string query_entity_index_key(object room, string entity_id)
{
    string instance_id;

    if (!objectp(room) || !stringp(entity_id) || entity_id == "")
        return "";
    instance_id = room->query("instance/id") || "public";
    return WORLD_KEY_PUBLIC + "|" + instance_id + "|" + base_name(room)
        + "|" + entity_id;
}

private string query_content_entity_index_key(object room, mapping entity)
{
    string entity_id;
    string instance_id;

    if (!objectp(room) || !mapp(entity))
        return "";
    entity_id = query_content_entity_id(entity);
    if (entity_id == "")
        return "";
    instance_id = room->query("instance/id") || "public";
    return WORLD_KEY_PUBLIC + "|" + instance_id + "|" + base_name(room)
        + "|" + entity_id;
}

/* 基于已 stamp 的实体 temp 构造索引 key（reindex 时无 entity mapping 可用）。 */
private string query_content_entity_slot_index_key(object room,
    mapping entity, int entity_slot)
{
    string index_key;

    index_key = query_content_entity_index_key(room, entity);
    if (index_key == "" || entity_slot <= 0)
        return index_key;
    return index_key + "|slot=" + entity_slot;
}

private string query_content_entity_index_key_for_ob(object room, object ob)
{
    mapping entity;
    string entity_id;
    int entity_slot;

    if (!objectp(room) || !objectp(ob))
        return "";
    entity_id = ob->query_temp("world_lifecycle/content_entity_id") || "";
    if (entity_id == "")
        return "";
    entity = ([ "id": entity_id ]);
    entity_slot = ob->query_temp("world_lifecycle/content_entity_slot");
    return query_content_entity_slot_index_key(room, entity, entity_slot);
}

/* Escort lease identity uses the same slot-aware key as the content index.
 * Callers without a managed content stamp retain the ordinary room/entity key. */
string query_entity_index_key_for_object(object room, object ob)
{
    string index_key;
    string entity_id;

    index_key = query_content_entity_index_key_for_ob(room, ob);
    if (index_key != "")
        return index_key;
    if (!objectp(ob))
        return "";
    entity_id = ob->query_temp("world_lifecycle/content_entity_id") || "";
    return query_entity_index_key(room, entity_id);
}

private object query_indexed_content_entity(object room, mapping entity)
{
    object ob;
    string expected_path;
    string index_key;
    int hp;
    int max_hp;

    index_key = query_content_entity_index_key(room, entity);
    if (index_key == "")
        return 0;
    if (!mapp(_room_entity_object_index))
        _room_entity_object_index = ([ ]);

    ob = _room_entity_object_index[index_key];
    expected_path = query_content_entity_path(entity);
    if (!objectp(ob) || environment(ob) != room || interactive(ob)
        || ob->query_temp("is_dying")) {
        /* slot 被 escort 租走（leased clone 在别处）——不删索引
         * 不补刷不登记冷却，保持 spawn slot 空置；补刷门控在
         * ensure_content_entity 用 query_slot_leased 拦截。 */
        if (objectp(ob) && environment(ob) != room
            && stringp(ob->query_temp("world_lifecycle/lease_token"))
            && mapp(_lease_slot_index)
            && _lease_slot_index[index_key]
                == ob->query_temp("world_lifecycle/lease_token"))
            return 0;
        /* 物品被玩家拾取（环境为玩家）时登记重生冷却；仅登记配置了
         * respawn_cooldown 的实体。give 交付给 NPC、销毁、首次生成均不登记。 */
        if (objectp(ob) && interactive(environment(ob))
            && stringp(ob->query("item_type"))
            && stringp(ob->query_temp("world_lifecycle/content_entity_id"))) {
            if (intp(_respawn_cooldown_seconds[index_key])
                && _respawn_cooldown_seconds[index_key] > 0)
                _respawn_cooldown_until[index_key] = time()
                    + _respawn_cooldown_seconds[index_key];
        }
        map_delete(_room_entity_object_index, index_key);
        /* 拾取/移出 room：同步清理 entity 反向映射（否则 O(1) resolver
         * 会残留指向已离房对象的条目）。 */
        if (mapp(_entity_object_index)
            && _entity_object_index[index_key] == ob)
            map_delete(_entity_object_index, index_key);
        return 0;
    }
    if (expected_path == ""
        || ob->query_temp("world_lifecycle/content_entity_path") != expected_path
        || base_name(ob) != expected_path) {
        remove_content_entity_index(ob);
        destruct(ob);
        return 0;
    }
    max_hp = ob->query("combat/max_hp");
    hp = ob->query("combat/hp");
    if (intp(max_hp) && max_hp > 0 && intp(hp) && hp <= 0) {
        map_delete(_room_entity_object_index, index_key);
        if (mapp(_entity_object_index)
            && _entity_object_index[index_key] == ob)
            map_delete(_entity_object_index, index_key);
        return 0;
    }
    return ob;
}

private void index_content_entity(object room, mapping entity, object ob)
{
    string index_key;
    string base_index_key;

    if (!objectp(ob))
        return;
    index_key = query_content_entity_index_key_for_ob(room, ob);
    if (index_key == "")
        return;
    base_index_key = query_content_entity_index_key(room, entity);
    if (index_key != base_index_key && intp(_respawn_cooldown_seconds[base_index_key]))
        _respawn_cooldown_seconds[index_key] = _respawn_cooldown_seconds[base_index_key];
    if (!mapp(_room_entity_object_index))
        _room_entity_object_index = ([ ]);
    _room_entity_object_index[index_key] = ob;
    if (!mapp(_entity_object_index))
        _entity_object_index = ([ ]);
    if (stringp(ob->query_temp("world_lifecycle/content_entity_id"))
        && ob->query_temp("world_lifecycle/content_entity_id") != "")
        _entity_object_index[index_key] = ob;
}

/* 日程移动等跨房间迁移后重索引：旧房 key 摘除、新房 key 登记，
 * 保证时辰边界窗口内新房 ensure 命中索引不补刷克隆（防双生）。
 * 仅处理已 stamp 的受管实体；未受管对象直接返回。 */
int reindex_content_entity(object ob, object old_room, object new_room)
{
    object room_indexed;
    object entity_indexed;
    string old_key;
    string new_key;
    int cooldown_seconds;
    int cooldown_until;

    if (!objectp(ob) || !objectp(old_room) || !objectp(new_room))
        return 0;
    if (ob->query_temp("world_lifecycle/content_entity_id") == "")
        return 0;
    old_key = query_content_entity_index_key_for_ob(old_room, ob);
    new_key = query_content_entity_index_key_for_ob(new_room, ob);
    if (old_key == "" || new_key == "" || old_key == new_key)
        return 0;

    room_indexed = mapp(_room_entity_object_index)
        ? _room_entity_object_index[new_key] : 0;
    entity_indexed = mapp(_entity_object_index) ? _entity_object_index[new_key] : 0;
    if (objectp(room_indexed) && room_indexed != ob) {
        if (environment(room_indexed) != 0)
            return 0;
        map_delete(_room_entity_object_index, new_key);
    }
    if (objectp(entity_indexed) && entity_indexed != ob) {
        if (environment(entity_indexed) != 0)
            return 0;
        map_delete(_entity_object_index, new_key);
    }

    if (intp(_respawn_cooldown_seconds[old_key]))
        cooldown_seconds = _respawn_cooldown_seconds[old_key];
    if (intp(_respawn_cooldown_until[old_key]))
        cooldown_until = _respawn_cooldown_until[old_key];
    if (mapp(_room_entity_object_index)
        && _room_entity_object_index[old_key] == ob)
        map_delete(_room_entity_object_index, old_key);
    if (mapp(_entity_object_index) && _entity_object_index[old_key] == ob)
        map_delete(_entity_object_index, old_key);
    if (cooldown_seconds > 0)
        _respawn_cooldown_seconds[new_key] = cooldown_seconds;
    if (cooldown_until > 0)
        _respawn_cooldown_until[new_key] = cooldown_until;
    map_delete(_respawn_cooldown_seconds, old_key);
    map_delete(_respawn_cooldown_until, old_key);

    ob->set_temp("world_lifecycle/content_entity_world_id", WORLD_KEY_PUBLIC);
    ob->set_temp("world_lifecycle/content_entity_instance_id",
        new_room->query("instance/id") || "public");
    ob->set_temp("world_lifecycle/content_entity_room_path", base_name(new_room));
    if (!mapp(_room_entity_object_index))
        _room_entity_object_index = ([ ]);
    _room_entity_object_index[new_key] = ob;
    if (!mapp(_entity_object_index))
        _entity_object_index = ([ ]);
    _entity_object_index[new_key] = ob;
    return 1;
}

private void remove_content_entity_index(object ob)
{
    object indexed;
    string index_key;

    if (!objectp(ob))
        return;
    if (mapp(_room_entity_object_index))
        foreach (index_key, indexed in _room_entity_object_index)
            if (!objectp(indexed) || indexed == ob)
                map_delete(_room_entity_object_index, index_key);
    if (mapp(_entity_object_index))
        foreach (index_key, indexed in _entity_object_index)
            if (!objectp(indexed) || indexed == ob)
                map_delete(_entity_object_index, index_key);
}

private string *query_desired_content_entity_ids(mapping *entities)
{
    mapping entity;
    string entity_id;
    string *entity_ids;

    entity_ids = ({ });
    foreach (entity in entities) {
        entity_id = query_content_entity_id(entity);
        if (entity_id != "" && member_array(entity_id, entity_ids) == -1)
            entity_ids += ({ entity_id });
    }
    return entity_ids;
}

private int prune_stale_managed_entities(object room, mapping *entities)
{
    mapping entity;
    object ob;
    string entity_id;
    string entity_path;
    string expected_path;
    string instance_id;
    string room_path;
    string lease_token;
    string *desired_entity_ids;
    int expected_count;
    int entity_slot;
    int pruned;

    if (!objectp(room))
        return 0;
    desired_entity_ids = query_desired_content_entity_ids(entities);
    instance_id = room->query("instance/id") || "public";
    room_path = base_name(room);
    foreach (ob in all_inventory(room)) {
        if (!objectp(ob) || interactive(ob) || userp(ob))
            continue;
        entity_id = ob->query_temp("world_lifecycle/content_entity_id") || "";
        entity_path = ob->query_temp("world_lifecycle/content_entity_path") || "";
        lease_token = ob->query_temp("world_lifecycle/lease_token") || "";
        if (lease_token != "")
            continue;
        if (entity_id == ""
            || ob->query_temp("world_lifecycle/content_entity_world_id") != WORLD_KEY_PUBLIC
            || ob->query_temp("world_lifecycle/content_entity_instance_id") != instance_id
            || ob->query_temp("world_lifecycle/content_entity_room_path") != room_path)
            continue;
        expected_path = "";
        expected_count = 0;
        foreach (entity in entities) {
            if (query_content_entity_id(entity) != entity_id)
                continue;
            expected_path = query_content_entity_path(entity);
            expected_count = intp(entity["count"]) && entity["count"] > 0
                ? entity["count"] : 1;
            break;
        }
        entity_slot = ob->query_temp("world_lifecycle/content_entity_slot");
        if (member_array(entity_id, desired_entity_ids) != -1
            && expected_path != ""
            && entity_path == expected_path
            && base_name(ob) == expected_path
            && (entity_slot <= 0 || entity_slot <= expected_count))
            continue;
        remove_content_entity_index(ob);
        destruct(ob);
        pruned++;
    }
    _content_entity_prune_count += pruned;
    return pruned;
}

private int content_entity_applied_for_generation(object ob, object room,
    mapping entity, int content_generation, int entity_slot)
{
    string expected_path;
    string instance_id;
    string world_id;

    if (!objectp(ob) || !objectp(room) || !mapp(entity))
        return 0;

    world_id = WORLD_KEY_PUBLIC;
    instance_id = room->query("instance/id") || "public";
    expected_path = query_content_entity_path(entity);
    return ob->query_temp("world_lifecycle/content_entity_world_id") == world_id
        && ob->query_temp("world_lifecycle/content_entity_instance_id") == instance_id
        && ob->query_temp("world_lifecycle/content_entity_generation") == content_generation
        && ob->query_temp("world_lifecycle/content_entity_room_path") == base_name(room)
        && ob->query_temp("world_lifecycle/content_entity_id") == query_content_entity_id(entity)
        && ob->query_temp("world_lifecycle/content_entity_slot") == entity_slot
        && ob->query_temp("world_lifecycle/content_entity_path") == expected_path
        && base_name(ob) == expected_path;
}

private void mark_content_entity_applied(object ob, object room, mapping entity,
    int content_generation, int entity_slot)
{
    string instance_id;
    string world_id;

    if (!objectp(ob) || !objectp(room) || !mapp(entity))
        return;

    world_id = WORLD_KEY_PUBLIC;
    instance_id = room->query("instance/id") || "public";
    ob->set_temp("world_lifecycle/content_entity_world_id", world_id);
    ob->set_temp("world_lifecycle/content_entity_instance_id", instance_id);
    ob->set_temp("world_lifecycle/content_entity_generation", content_generation);
    ob->set_temp("world_lifecycle/content_entity_room_path", base_name(room));
    ob->set_temp("world_lifecycle/content_entity_id", query_content_entity_id(entity));
    ob->set_temp("world_lifecycle/content_entity_slot", entity_slot);
    ob->set_temp("world_lifecycle/content_entity_count",
        intp(entity["count"]) && entity["count"] > 0 ? entity["count"] : 1);
    ob->set_temp("world_lifecycle/content_entity_path",
        query_content_entity_path(entity));
}

private int clear_entity_field(object ob, string path)
{
    mixed value;

    if (!objectp(ob) || !stringp(path) || path == "")
        return 0;
    value = ob->query(path);
    if ((stringp(value) && value != "") || (intp(value) && value != 0)
        || mapp(value) || arrayp(value))
        return function_exists("delete", ob) ? ob->delete(path) : 0;
    return 1;
}

private int initialize_content_entity(object room, mapping entity, object ob,
    int content_generation, int entity_slot)
{
    mapping template;
    mixed apply_result;
    mixed err;
    mixed value;
    int ok;

    if (!objectp(room) || !mapp(entity) || !objectp(ob))
        return 0;
    if (content_entity_applied_for_generation(ob, room, entity,
        content_generation, entity_slot)) {
        index_content_entity(room, entity, ob);
        return 1;
    }

    ok = 1;
    if (function_exists("apply_content_entity", ob)) {
        err = catch(apply_result = call_other(ob, "apply_content_entity", entity));
        if (err || (intp(apply_result) && apply_result != 1)) {
            log_file("system/world", sprintf("[%s] content entity apply failed for %s: %O\n",
                ctime(time())[4..19], file_name(ob), err ? err : apply_result));
            return 0;
        }
    }

    value = entity["loot_table_id"];
    if (stringp(value) && value != "")
        ok = function_exists("set", ob) && ob->set("loot_table_id", value) && ok;
    else
        ok = clear_entity_field(ob, "loot_table_id") && ok;
    value = entity["quest_kill_target"];
    if (stringp(value) && value != "")
        ok = function_exists("set", ob) && ob->set("quest_kill_target", value) && ok;
    else
        ok = clear_entity_field(ob, "quest_kill_target") && ok;

    if (stringp(entity["template_id"]) && entity["template_id"] != "") {
        template = CONTENT_REGISTRY_D->query_npc_template(entity["template_id"]);
        if (mapp(template)) {
            if (mapp(template["schedule"]))
                ok = function_exists("set", ob)
                    && ob->set("schedule", template["schedule"]) && ok;
            else
                ok = clear_entity_field(ob, "schedule") && ok;
            if (stringp(template["schedule_default"])
                && template["schedule_default"] != "")
                ok = function_exists("set", ob)
                    && ob->set("schedule_default", template["schedule_default"]) && ok;
            else
                ok = clear_entity_field(ob, "schedule_default") && ok;
        }
    }
    if (!ok)
        return 0;
    mark_content_entity_applied(ob, room, entity, content_generation, entity_slot);
    index_content_entity(room, entity, ob);
    NPC_SCHEDULE_D->maybe_track_npc(ob);
    return 1;
}

private int assign_content_entity_slot(object ob, int count, int *used_slots)
{
    int entity_slot;

    if (!objectp(ob) || count <= 0 || !arrayp(used_slots))
        return 0;
    entity_slot = ob->query_temp("world_lifecycle/content_entity_slot");
    if (intp(entity_slot) && entity_slot > 0 && entity_slot <= count
        && !used_slots[entity_slot])
        return entity_slot;
    for (entity_slot = 1; entity_slot <= count; entity_slot++)
        if (!used_slots[entity_slot])
            return entity_slot;
    return 0;
}

private int ensure_content_entity(object room, mapping entity, int content_generation)
{
    object ob;
    mapping instance;
    string path;
    string present_id;
    string index_key;
    string entity_id;
    mixed *instances;
    int *used_slots;
    int count;
    int entity_slot;
    int cooldown_seconds;
    int cooldown_until;
    int ensured;

    if (!objectp(room) || !mapp(entity))
        return 0;
    /* NPC 日程：模板当前时辰应在其他房间时，不在此房间补刷 */
    if (NPC_SCHEDULE_D->should_skip_room(room, entity))
        return 0;
    path = entity["path"] || "";
    present_id = entity["present_id"] || entity["id"] || "";
    entity_id = query_content_entity_id(entity);
    /* LPC 语义坑：mapping 缺键返回 int 0，intp(0)=1 会把缺省 count
     * 误判为 0 并拦截补刷（老陈 spawn 失效根因）。缺键/非正数一律按 1。 */
    count = intp(entity["count"]) && entity["count"] > 0 ? entity["count"] : 1;
    if (!stringp(path) || path == "" || entity_id == "" || count <= 0)
        return 0;
    // 冷却按完整 spawn 槽隔离，拾取收据负责登记，到期由 owner timer 补刷。
    cooldown_seconds = intp(entity["respawn_cooldown"]) ? entity["respawn_cooldown"] : 0;
    index_key = query_content_entity_index_key(room, entity);
    if (index_key != "")
        _respawn_cooldown_seconds[index_key] = cooldown_seconds;
    if (count > 1) {
        /* 数量实体只从本槽已登记的对象或本次新建对象取候选；不把同
         * 路径的别名对象认领为本槽实体。 */
        if (query_slot_leased(room, entity_id))
            return 0;
        instances = call_other("/lib/std/utils/util_view",
            "ensure_room_object_count", room, path, entity_id, count);
        if (!arrayp(instances) || sizeof(instances) < count)
            return 0;
        used_slots = allocate(count + 1);
        /* 先保留仍在范围内的旧 slot，再给新对象/旧无 slot 对象分配
         * 空槽；不能让 all_inventory 顺序把 slot 3 重排成 slot 1，
         * 否则 count 缩小时会留下重复 slot。 */
        foreach (instance in instances) {
            ob = mapp(instance) ? instance["object"] : 0;
            if (!objectp(ob))
                continue;
            entity_slot = ob->query_temp("world_lifecycle/content_entity_slot");
            if (intp(entity_slot) && entity_slot > 0 && entity_slot <= count
                && !used_slots[entity_slot]) {
                instance["assigned_slot"] = entity_slot;
                used_slots[entity_slot] = 1;
            }
        }
        foreach (instance in instances) {
            ob = mapp(instance) ? instance["object"] : 0;
            if (!objectp(ob) || instance["assigned_slot"])
                continue;
            entity_slot = assign_content_entity_slot(ob, count, used_slots);
            if (entity_slot <= 0)
                continue;
            instance["assigned_slot"] = entity_slot;
            used_slots[entity_slot] = 1;
        }
        foreach (instance in instances) {
            ob = mapp(instance) ? instance["object"] : 0;
            entity_slot = mapp(instance) ? instance["assigned_slot"] : 0;
            if (!objectp(ob) || entity_slot <= 0)
                continue;
            if (initialize_content_entity(room, entity, ob,
                content_generation, entity_slot))
                ensured++;
        }
        return ensured == count ? 1 : 0;
    }
    ob = query_indexed_content_entity(room, entity);
    if (!objectp(ob)) {
        /* slot 被 escort 租走时不补刷（fail-closed：宁可缺刷
         * 不可双生）。索引 miss 且无租约时才生成新 clone。 */
        if (query_slot_leased(room, entity_id))
            return 0;
        if (index_key != "" && intp(_respawn_cooldown_until[index_key])) {
            cooldown_until = _respawn_cooldown_until[index_key];
            if (time() < cooldown_until) {
                schedule_respawn_refresh(room, index_key, cooldown_until);
                return 0;
            }
            map_delete(_respawn_cooldown_until, index_key);
        }
        ob = call_other("/lib/std/utils/util_view", "ensure_room_object", room,
            present_id, path);
    }
    if (!objectp(ob))
        return 0;
    if (stringp(ob->query_temp("world_lifecycle/content_entity_id"))
        && ob->query_temp("world_lifecycle/content_entity_id") != entity_id)
        return 0;
    return initialize_content_entity(room, entity, ob, content_generation, 0);
}

private int ensure_room_entities_impl(object room, mapping timing_ns)
{
    mapping entity;
    mapping *entities;
    int content_generation;
    int ensured;
    int pruned;
    int now;
    int phase_finished_at;
    int phase_started_at;
    int profile_timing;
    int same_tick_cached;
    mapping owner_route;
    string room_path;
    string world_id;
    string instance_id;

    if (!objectp(room))
        return 0;

    profile_timing = mapp(timing_ns);
    if (profile_timing)
        phase_started_at = perf_counter_ns();

    now = time();
    room_path = INSTANCE_ROOM_D->query_template_room_path(room);
    world_id = WORLD_KEY_PUBLIC;
    instance_id = room->query("instance/id") || "public";
    content_generation = CONTENT_REGISTRY_D->query_content_generation();
    same_tick_cached = room->query_temp("world_lifecycle/entities_checked_at") == now
        && room->query_temp("world_lifecycle/entities_world_id") == world_id
        && room->query_temp("world_lifecycle/entities_instance_id") == instance_id
        && room->query_temp("world_lifecycle/entities_room_path") == room_path
        && room->query_temp("world_lifecycle/entities_content_generation") == content_generation
        && mapp(room->query_temp("owner/world_room_route"))
        && room->query_temp("owner/world_room_route")["ok"] == 1;
    if (profile_timing) {
        phase_finished_at = perf_counter_ns();
        timing_ns["gate"] = phase_finished_at - phase_started_at;
        phase_started_at = phase_finished_at;
    }
    if (same_tick_cached) {
        if (profile_timing) {
            timing_ns["owner_route"] = 0;
            timing_ns["entities"] = 0;
        }
        return room->query_temp("world_lifecycle/entities_present") ? 1 : 0;
    }

    owner_route = stamp_world_room_owner_route(room, "ensure_room_entities");
    if (profile_timing) {
        phase_finished_at = perf_counter_ns();
        timing_ns["owner_route"] = phase_finished_at - phase_started_at;
        phase_started_at = phase_finished_at;
    }
    if (!mapp(owner_route) || owner_route["ok"] != 1) {
        log_file("system/world", sprintf("[%s] room entity ensure blocked by owner route for %s: %O\n",
            ctime(time())[4..19], room_path,
            mapp(owner_route) ? owner_route["failure_reason"] : "route unavailable"));
        return 0;
    }

    entities = query_cached_room_entities(room_path);
    /* 共享房间实体由全局生命周期 owner 补刷；玩家策略只在
     * projection/interaction authorization 层过滤，不得改变物理 spawn 集合。 */
    pruned = prune_stale_managed_entities(room, entities);
    foreach (entity in entities)
        ensured |= ensure_content_entity(room, entity, content_generation);
    /* 世界事件对象补刷：实例房间与主房间保持事件窗口一致（含窗口过后清理） */
    ensured |= WORLD_EVENT_D->ensure_event_present(room);
    room->set_temp("world_lifecycle/entities_checked_at", now);
    room->set_temp("world_lifecycle/entities_world_id", world_id);
    room->set_temp("world_lifecycle/entities_instance_id", instance_id);
    room->set_temp("world_lifecycle/entities_room_path", room_path);
    room->set_temp("world_lifecycle/entities_content_generation", content_generation);
    room->set_temp("world_lifecycle/entities_present", ensured ? 1 : 0);
    room->set_temp("world_lifecycle/entities_pruned", pruned);
    if (profile_timing)
        timing_ns["entities"] = perf_counter_ns() - phase_started_at;
    return ensured ? 1 : 0;
}

int ensure_room_entities(object room)
{
    return ensure_room_entities_impl(room, 0);
}

int ensure_room_entities_with_timing(object room, mapping timing_ns)
{
    return ensure_room_entities_impl(room, timing_ns);
}

void ensure_room_baseline(object room)
{
    ensure_room_entities(room);
}

/* 任务物品交付给 NPC 后清除重生冷却，允许房间立即刷新（给下一位玩家）。 */
void clear_respawn_cooldown(object ob)
{
    string entity_id;
    string room_path;
    string index_key;
    mapping pending;
    object room;
    int entity_slot;

    if (!objectp(ob))
        return;
    entity_id = ob->query_temp("world_lifecycle/content_entity_id") || "";
    room_path = ob->query_temp("world_lifecycle/content_entity_room_path") || "";
    entity_slot = ob->query_temp("world_lifecycle/content_entity_slot");
    if (entity_id == "" || room_path == "")
        return;
    index_key = WORLD_KEY_PUBLIC + "|"
        + (ob->query_temp("world_lifecycle/content_entity_instance_id") || "public")
        + "|" + room_path + "|" + entity_id;
    if (entity_slot > 0)
        index_key += "|slot=" + entity_slot;
    pending = _respawn_timers[index_key];
    room = mapp(pending) ? find_object(pending["room"]) : 0;
    if (objectp(room)) {
        _respawn_cooldown_until[index_key] = time();
        schedule_respawn_refresh(room, index_key, _respawn_cooldown_until[index_key]);
    } else {
        map_delete(_respawn_cooldown_until, index_key);
        if (mapp(pending))
            TIMER_D->cancel_timer(pending["handle"]);
        map_delete(_respawn_timers, index_key);
    }
}

private int synthetic_scope_registered(string scope_key)
{
    mapping region;

    if (!stringp(scope_key) || scope_key == "")
        return 0;
    foreach (region in values(REGION_D->query_all_regions() || ([ ])))
        if (mapp(region) && region["enabled"]
            && region["scope_key"] == scope_key)
            return 1;
    return 0;
}

mapping synthetic_entity_transition(string scope_key, string entity_id,
    string action, int generation)
{
    mapping current;
    mapping next;
    string flag_key;
    string operation_id;

    if (!synthetic_scope_registered(scope_key) || !stringp(entity_id)
        || entity_id == "" || (action != "create" && action != "destroy")
        || !intp(generation) || generation <= 0)
        return ([ "ok": 0, "error": "entity_args_invalid" ]);
    flag_key = "synthetic_entity/" + entity_id;
    current = WORLD_STATE_D->query_scope_flag(scope_key, flag_key);
    if (mapp(current) && current["state"] == "tombstoned")
        return action == "destroy"
            ? ([ "ok": 1, "duplicate": 1, "state": "tombstoned",
                "scope_key": scope_key, "entity_id": entity_id ])
            : ([ "ok": 0, "error": "entity_tombstoned",
                "scope_key": scope_key, "entity_id": entity_id ]);
    if (mapp(current) && current["state"] == "active") {
        if (action == "create" && current["generation"] == generation)
            return ([ "ok": 1, "duplicate": 1, "state": "active",
                "scope_key": scope_key, "entity_id": entity_id,
                "generation": generation ]);
        if (generation <= (current["generation"] || 0))
            return ([ "ok": 0, "error": "stale_entity_generation",
                "scope_key": scope_key, "entity_id": entity_id,
                "generation": generation ]);
    }
    operation_id = "world-entity/" + scope_key + "/" + entity_id
        + "/" + action + "/" + sprintf("%d", generation);
    next = ([
        "scope_key": scope_key,
        "entity_id": entity_id,
        "generation": generation,
        "state": action == "create" ? "active" : "tombstoned",
        "operation_id": operation_id,
        "updated_at": time(),
    ]);
    if (!WORLD_STATE_D->set_scope_flag_with_operation(scope_key, flag_key,
        next, operation_id))
        return ([ "ok": 0, "error": "entity_state_persist_failed",
            "recovery_required": 1, "scope_key": scope_key,
            "entity_id": entity_id, "generation": generation ]);
    return ([ "ok": 1, "duplicate": 0,
        "state": next["state"], "scope_key": scope_key,
        "entity_id": entity_id, "generation": generation,
        "operation_id": next["operation_id"] ]);
}

mapping query_synthetic_entity_snapshot(string scope_key, string entity_id)
{
    mapping entity;
    string flag_key;

    if (!synthetic_scope_registered(scope_key) || !stringp(entity_id)
        || entity_id == "")
        return ([ "ok": 0, "error": "entity_args_invalid" ]);
    flag_key = "synthetic_entity/" + entity_id;
    entity = WORLD_STATE_D->query_scope_flag(scope_key, flag_key);
    return ([
        "ok": 1,
        "scope_key": scope_key,
        "entity_id": entity_id,
        "entity": mapp(entity) ? entity : ([ ]),
        "state": mapp(entity) ? entity["state"] : "missing",
    ]);
}

mapping query_health_snapshot()
{
    return ([
        "room_cache_count": sizeof(_room_entities_cache || ([ ])),
        "room_entity_index_count": sizeof(_room_entity_object_index || ([ ])),
        "entity_index_count": sizeof(_entity_object_index || ([ ])),
        "lease_token_count": sizeof(_lease_token_index || ([ ])),
        "lease_slot_count": sizeof(_lease_slot_index || ([ ])),
        "content_entity_prune_count": _content_entity_prune_count,
        "runtime_epoch": _runtime_epoch || "",
    ]);
}

void create()
{
    ::create();
    _room_entities_cache = ([ ]);
    _room_entity_object_index = ([ ]);
    _entity_object_index = ([ ]);
    _lease_token_index = ([ ]);
    _lease_slot_index = ([ ]);
    _runtime_epoch = sprintf("%d", time() - uptime());
    _respawn_cooldown_seconds = ([ ]);
    _respawn_cooldown_until = ([ ]);
    _respawn_timers = ([ ]);
    _content_entity_prune_count = 0;
    CONFIG_D->compat_register_config_callback("content", "on_config_reloaded", this_object());
}

void on_config_reloaded(string key, string config_name)
{
    if (key == "content" || config_name == "content")
        _room_entities_cache = ([ ]);
}

void on_config_reload_rollback(string key, string config_name)
{
    if (key == "content" || config_name == "content")
        _room_entities_cache = ([ ]);
}

int query_content_entity_prune_count()
{
    return _content_entity_prune_count;
}
