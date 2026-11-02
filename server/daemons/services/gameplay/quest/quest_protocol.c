/**
 * @file    quest_protocol.c
 * @brief   任务域 XK 协议发送与 payload 构建真源
 *
 * 自 kernel/simul_efun_player_xk.c 迁入的 quest payload 构建簇与三个
 * 领域发送器（xk_send_quest_list / xk_query_quest_objective_projection /
 * xk_send_quest_update）。发射路径契约：保持既有
 * XK_PROTOCOL_CORE->xk_send 直发语义，不改走事务队列。
 */

#pragma strict_types

#include <system/services.h>
#include <system/xk_protocol.h>
#include <game/quest.h>

#define XK_PROTOCOL_CORE "/kernel/framework/xk_protocol_core"

// ===== quest-list 读模型缓存（B2）=====
// 键：player id；值：({ content_generation, domain_revision,
// projection_revision, room_path, payload })。失效挂四代元组任一变化——
// 位置维必需：next_step/target_room 随 current_room 变化（跨步选取
// 跳过所在房间的 ARRIVE 步、ESCORT 分支回填 current_room）；
// publish-time live-token recheck 在发送前重验 interactive。
// 容量护栏：超限整体重置（读模型可全量重建，无一致性风险）。
// 缓存体不可变契约：命中路径返回前浅拷贝 quests 层，消费方不得改写。
// 失效分工：push 钩子（quest_d 修订点→invalidate）仅内存卫生；
// 正确性权威在 pull 侧四代元组比对——content reload 等路径无需另配 push。
nosave mapping _quest_list_read_model = ([]);
nosave int _QUEST_LIST_MODEL_LIMIT = 128;
nosave int _quest_list_hits = 0;
nosave int _quest_list_misses = 0;

// 前置声明（strict_types：缓存块先于下方定义使用）
mapping build_quest_list_payload(object me);
string query_player_room_path(object me);
private mapping quest_read_model_validated(object me);

private string query_player_id(object player)
{
    return objectp(player) ? PLAYER_STATE_D->query_player_id(player) : "";
}

private mixed *quest_list_cache_lookup(object me, int content_gen,
    int domain_rev, int projection_rev, string room_path)
{
    mixed *cached;

    if (!objectp(me))
        return 0;
    cached = _quest_list_read_model[query_player_id(me)];
    if (!arrayp(cached) || sizeof(cached) != 5)
        return 0;
    if (cached[0] != content_gen || cached[1] != domain_rev
        || cached[2] != projection_rev || cached[3] != room_path) {
        _quest_list_misses++;
        return 0;
    }
    _quest_list_hits++;
    return cached;
}

private void quest_list_cache_store(object me, int content_gen,
    int domain_rev, int projection_rev, string room_path, mapping payload)
{
    string user_id;

    if (!objectp(me) || !mapp(payload))
        return;
    if (sizeof(_quest_list_read_model) >= _QUEST_LIST_MODEL_LIMIT)
        _quest_list_read_model = ([]);
    user_id = query_player_id(me);
    if (!stringp(user_id) || user_id == "")
        return;
    _quest_list_read_model[user_id] = ({ content_gen, domain_rev,
        projection_rev, room_path, payload });
}

/** 读模型失效（双保险位：quest_d.c finish_quest_mutation 修订推进点调用）。 */
public void invalidate_quest_list_read_model(string user_id)
{
    /* 空串按全量重置处理：调用点传的是 query_player_id(player)，不可用时为空串。
     * 原实现让空串落进「既非有效 id、又非 falsy」的空操作分支，缓存永不失效。 */
    if (stringp(user_id) && user_id != "")
        map_delete(_quest_list_read_model, user_id);
    else
        _quest_list_read_model = ([]);
}

/**
 * 私有快照读：一次重验四代元组，命中返回缓存 payload（引用共享，勿改写），
 * 未命中返 0。循环型消费者应先取本快照再自行索引，避免逐条目重复校验。
 */
private mapping quest_read_model_validated(object me)
{
    mixed *cached;
    mapping pd;
    mapping projection;

    if (!objectp(me))
        return 0;
    cached = _quest_list_read_model[query_player_id(me)];
    if (!arrayp(cached) || sizeof(cached) != 5 || !mapp(cached[4])) {
        _quest_list_misses++;
        return 0;
    }
    if (cached[0] != CONTENT_REGISTRY_D->query_content_generation()) {
        _quest_list_misses++;
        return 0;
    }
    pd = QUEST_D->ensure_player_data(me);
    if (cached[1] != (mapp(pd) ? (pd["domain_revision"] || 0) : -1)) {
        _quest_list_misses++;
        return 0;
    }
    projection = LOGIN_PROJECTION_D->query_projection_identity(me);
    if (cached[2] != (mapp(projection)
        ? (projection["projection_revision"] || 0) : -1)) {
        _quest_list_misses++;
        return 0;
    }
    if (cached[3] != query_player_room_path(me)) {
        _quest_list_misses++;
        return 0;
    }
    _quest_list_hits++;
    return cached[4];
}

/** 单条目薄包装：先取已验证快照再索引条目（引用共享，勿改写）。
 * 当前无调用方（objective 循环已直用 validated）；保留作外部单条目查询
 * 面，复活条件＝出现跨文件消费者。 */
public mapping quest_read_model_entry(object me, string quest_id)
{
    mapping payload;

    payload = quest_read_model_validated(me);
    if (!mapp(payload))
        return 0;
    return mapp(payload[quest_id]) ? payload[quest_id] : 0;
}

public int query_quest_list_read_model_size()
{
    return sizeof(_quest_list_read_model);
}

/** 读模型命中统计（measure 诊断面消费；hit＝四代元组全匹配复用）。 */
public mapping query_quest_list_read_model_stats()
{
    return ([
        "entries": sizeof(_quest_list_read_model),
        "hits": _quest_list_hits,
        "misses": _quest_list_misses,
    ]);
}

string query_quest_status_label(int status)
{
    switch (status) {
    case QUEST_STATUS_ACCEPTED:
        return "进行中";
    case QUEST_STATUS_COMPLETED:
        return "可提交";
    case QUEST_STATUS_DELIVERED:
        return "已完成";
    case QUEST_STATUS_FAILED:
        return "失败/放弃";
    default:
        return "可接取";
    }
}

string query_quest_provider_name(mapping quest_def)
{
    string provider_id;
    string name;

    provider_id = mapp(quest_def) && stringp(quest_def["provider"])
        ? quest_def["provider"]
        : "";
    if (provider_id == "")
        return "";
    /* L-37：provider 姓名读当前内容身份（room_entities/npc_templates 的
     * name），不再内嵌旧 provider 姓名表；内容缺失时返回空，明确缺失，
     * 不猜旧名、不恢复旧 NPC。 */
    name = CONTENT_REGISTRY_D->query_quest_provider_display_name(provider_id);
    return stringp(name) ? name : "";
}

string query_quest_summary(mapping quest_def, mapping quest_info)
{
    mixed *steps;
    mixed *objectives;
    mapping step_state;
    mapping objective;

    /* summary 是详情面板"描述"行的数据源，必须回归
     * 叙事文本（quest_def.description）；原先未完成步 desc 优先的取值序会让描述行
     * 显示当前步提示、与目标节重复。步进提示由 next_step_label 承载。 */
    if (mapp(quest_def) && stringp(quest_def["description"]) && quest_def["description"] != "")
        return quest_def["description"];

    steps = mapp(quest_info) && arrayp(quest_info["steps"])
        ? quest_info["steps"]
        : ({ });
    foreach (step_state in steps) {
        if (!mapp(step_state) || step_state["completed"])
            continue;
        if (stringp(step_state["desc"]) && step_state["desc"] != "")
            return step_state["desc"];
    }

    objectives = mapp(quest_def) && arrayp(quest_def["objectives"])
        ? quest_def["objectives"]
        : ({ });
    foreach (objective in objectives) {
        if (mapp(objective) && stringp(objective["desc"])
            && objective["desc"] != "")
            return objective["desc"];
    }

    if (mapp(quest_def) && stringp(quest_def["description"]) && quest_def["description"] != "")
        return quest_def["description"];
    return "";
}

int query_current_quest_step_index(mapping quest_info)
{
    mixed *steps;
    int index;

    if (!mapp(quest_info) || !arrayp(quest_info["steps"]))
        return -1;

    steps = quest_info["steps"];
    index = intp(quest_info["current_step"]) ? quest_info["current_step"] : -1;
    if (index >= 0 && index < sizeof(steps) && mapp(steps[index])
        && !(steps[index]["completed"] || 0))
        return index;

    for (index = 0; index < sizeof(steps); index++) {
        mapping step_state;

        step_state = steps[index];
        if (mapp(step_state) && !(step_state["completed"] || 0))
            return index;
    }

    return -1;
}

mapping query_current_quest_step(mapping quest_info)
{
    mixed *steps;
    int index;

    if (!mapp(quest_info) || !arrayp(quest_info["steps"]))
        return ([]);

    steps = quest_info["steps"];
    index = query_current_quest_step_index(quest_info);
    if (index >= 0 && index < sizeof(steps) && mapp(steps[index]))
        return steps[index];
    return ([]);
}

string query_player_room_path(object me)
{
    object env;

    if (!objectp(me))
        return "";
    env = environment(me);
    return objectp(env) ? base_name(env) : "";
}

mapping query_trackable_quest_step(mapping quest_info, object me)
{
    mixed *steps;
    mapping fallback;
    string current_room;
    int index;
    int cursor;

    if (!mapp(quest_info) || !arrayp(quest_info["steps"]))
        return ([]);

    steps = quest_info["steps"];
    index = query_current_quest_step_index(quest_info);
    if (index < 0 || index >= sizeof(steps))
        return ([]);

    current_room = query_player_room_path(me);
    fallback = mapp(steps[index]) ? steps[index] : 0;
    for (cursor = index; cursor < sizeof(steps); cursor++) {
        mapping step_state;

        step_state = steps[cursor];
        if (!mapp(step_state) || (step_state["completed"] || 0))
            continue;
        if (step_state["type"] == QUEST_STEP_ARRIVE
            && stringp(step_state["room"]) && step_state["room"] != ""
            && step_state["room"] == current_room)
            continue;
        return ([ "step": step_state, "index": cursor ]);
    }

    return mapp(fallback) ? ([ "step": fallback, "index": index ]) : ([]);
}

string query_quest_room_title(string room_path)
{
    mapping room;

    if (!stringp(room_path) || room_path == "")
        return "";
    room = CONTENT_REGISTRY_D->query_room(room_path);
    if (mapp(room) && stringp(room["title"]) && room["title"] != "")
        return room["title"];
    return "";
}

string query_quest_entity_name(string entity_id)
{
    mapping template;

    if (!stringp(entity_id) || entity_id == "")
        return "";

    template = CONTENT_REGISTRY_D->query_npc_template(entity_id);
    if (mapp(template) && stringp(template["name"]) && template["name"] != "")
        return template["name"];
    template = CONTENT_REGISTRY_D->query_item_template(entity_id);
    if (mapp(template) && stringp(template["name"]) && template["name"] != "")
        return template["name"];
    return "";
}

string query_quest_step_target_id(mapping step_state)
{
    string target;

    if (!mapp(step_state))
        return "";
    target = stringp(step_state["target"]) ? step_state["target"] : "";
    if (target != "")
        return target;
    return stringp(step_state["item_id"]) ? step_state["item_id"] : "";
}

string query_previous_quest_step_room(mixed *steps, int index)
{
    int cursor;

    if (!arrayp(steps) || index <= 0)
        return "";

    for (cursor = index - 1; cursor >= 0; cursor--) {
        mapping step_state;
        string room_path;

        step_state = steps[cursor];
        if (!mapp(step_state))
            continue;
        room_path = stringp(step_state["room"]) ? step_state["room"] : "";
        if (room_path != "")
            return room_path;
    }
    return "";
}

string query_quest_step_target_room(mapping step_state, string target_id,
    string current_room, string context_room)
{
    mapping rooms;
    string *room_paths;
    string room_path;

    if (!mapp(step_state))
        return "";

    /* 押送/护送：追踪先指向目标 NPC 所在房间（找到人再押往目的地） */
    if (step_state["type"] == QUEST_STEP_ESCORT && stringp(target_id) && target_id != "") {
        rooms = CONTENT_REGISTRY_D->query_entity_rooms(target_id);
        if (mapp(rooms) && sizeof(rooms) > 0) {
            room_paths = sort_array(keys(rooms), 1);
            if (stringp(current_room) && current_room != ""
                && member_array(current_room, room_paths) != -1)
                return current_room;
            return room_paths[0];
        }
    }

    room_path = stringp(step_state["room"]) ? step_state["room"] : "";
    if (room_path != "")
        return room_path;

    if (!stringp(target_id) || target_id == "")
        return stringp(context_room) ? context_room : "";
    rooms = CONTENT_REGISTRY_D->query_entity_rooms(target_id);
    if (!mapp(rooms) || sizeof(rooms) == 0)
        return stringp(context_room) ? context_room : "";
    room_paths = sort_array(keys(rooms), 1);
    if (stringp(current_room) && current_room != ""
        && member_array(current_room, room_paths) != -1)
        return current_room;
    return sizeof(room_paths) > 0 ? room_paths[0] : "";
}

string query_recommended_quest_command(mapping step_state)
{
    string command;
    string target;
    string item_id;
    string topic;
    string action;
    string room_path;

    if (!mapp(step_state))
        return "";

    if (step_state["service_event"])
        return "";

    command = stringp(step_state["command"]) ? step_state["command"] : "";
    if (command != "")
        return command;

    target = stringp(step_state["target"]) ? step_state["target"] : "";
    item_id = stringp(step_state["item_id"]) ? step_state["item_id"] : "";

    switch (step_state["type"]) {
    case QUEST_STEP_TALK:
        return target != "" ? "talk " + target : "";
    case QUEST_STEP_ASK:
        topic = stringp(step_state["topic"]) ? step_state["topic"] : "";
        return target != "" && topic != "" ? "ask " + target + " about " + topic : "";
    case QUEST_STEP_LOOK:
        return target != "" ? "look " + target : "look";
    case QUEST_STEP_ACTION:
        action = stringp(step_state["action"]) ? step_state["action"] : "";
        if (action == "")
            return "";
        return target != "" ? "quest action " + action + " " + target : "quest action " + action;
    case QUEST_STEP_ARRIVE:
        room_path = stringp(step_state["room"]) ? step_state["room"] : "";
        return room_path != "" ? "route " + room_path : "";
    case QUEST_STEP_GIVE:
        return item_id != "" && target != "" ? "give " + item_id + " to " + target : "";
    case QUEST_STEP_COLLECT:
        return item_id != "" ? "get " + item_id : "";
    case QUEST_STEP_CONSUME:
        action = stringp(step_state["action"]) ? step_state["action"] : "";
        return action != "" && item_id != "" ? action + " " + item_id : "";
    case QUEST_STEP_KILL:
        return target != "" ? "attack " + target : "";
    default:
        return "";
    }
}

string query_quest_target_primary_room(string target_id, string current_room)
{
    mapping rooms;
    string *room_paths;

    if (!stringp(target_id) || target_id == "")
        return "";

    rooms = CONTENT_REGISTRY_D->query_entity_rooms(target_id);
    if (!mapp(rooms) || sizeof(rooms) == 0)
        return "";

    room_paths = sort_array(keys(rooms), 1);
    if (stringp(current_room) && current_room != ""
        && member_array(current_room, room_paths) != -1) {
        return current_room;
    }
    return sizeof(room_paths) > 0 ? room_paths[0] : "";
}

mapping query_completed_quest_turn_in_payload(object me, string quest_id,
    mapping quest_def)
{
    string provider_id;
    string provider_name;
    string current_room;
    string room_path;

    provider_id = mapp(quest_def) && stringp(quest_def["provider"])
        ? quest_def["provider"]
        : "";
    provider_name = query_quest_provider_name(quest_def);
    current_room = query_player_room_path(me);
    room_path = query_quest_target_primary_room(provider_id, current_room);

    return ([
        "next_step_label": provider_name != ""
            ? "回去找" + provider_name + "交付"
            : "回去交付任务",
        "target_room_id": room_path,
        "target_room_name": query_quest_room_title(room_path),
        "target_entity_id": provider_id,
        "target_entity_name": provider_name,
        "recommended_command": "",
        "can_navigate": room_path != "",
    ]);
}

mapping query_quest_next_step_payload(object me, string quest_id,
    mapping quest_def, mapping quest_info)
{
    mixed *steps;
    mapping tracking_step;
    mapping step_state;
    string room_path;
    string target_id;
    string current_room;
    string context_room;
    int status;
    int step_index;

    status = mapp(quest_info) ? (quest_info["status"] || QUEST_STATUS_AVAILABLE) : QUEST_STATUS_AVAILABLE;
    if (status == QUEST_STATUS_COMPLETED) {
        return query_completed_quest_turn_in_payload(me, quest_id, quest_def);
    }

    tracking_step = query_trackable_quest_step(quest_info, me);
    step_state = mapp(tracking_step) ? tracking_step["step"] : query_current_quest_step(quest_info);
    if (!mapp(step_state) || sizeof(keys(step_state)) == 0)
        return ([]);

    steps = mapp(quest_info) && arrayp(quest_info["steps"]) ? quest_info["steps"] : ({ });
    step_index = mapp(tracking_step) && intp(tracking_step["index"]) ? tracking_step["index"] : -1;
    current_room = query_player_room_path(me);
    context_room = query_previous_quest_step_room(steps, step_index);
    target_id = query_quest_step_target_id(step_state);
    room_path = query_quest_step_target_room(step_state, target_id, current_room, context_room);
    return ([
        "next_step_label": stringp(step_state["desc"]) ? step_state["desc"] : "",
        "target_room_id": room_path,
        "target_room_name": query_quest_room_title(room_path),
        "target_entity_id": target_id,
        "target_entity_name": query_quest_entity_name(target_id),
        "recommended_command": query_recommended_quest_command(step_state),
        "can_navigate": room_path != "",
    ]);
}

mapping query_quest_rewards_payload(mapping quest_def)
{
    mapping rewards;

    rewards = mapp(quest_def) && mapp(quest_def["rewards"]) ? quest_def["rewards"] : ([]);
    return copy(rewards);
}

int resolve_quest_total(mapping quest_def, mapping quest_info)
{
    mixed *objectives;
    mapping objective;
    int total;

    if (mapp(quest_info) && intp(quest_info["total"]) && quest_info["total"] > 0)
        return quest_info["total"];

    objectives = mapp(quest_def) && arrayp(quest_def["objectives"])
        ? quest_def["objectives"]
        : ({ });
    total = 0;
    foreach (objective in objectives) {
        int objective_total;

        if (!mapp(objective))
            continue;
        objective_total = objective["count"];
        if (intp(objective_total) && objective_total > total)
            total = objective_total;
    }

    return total;
}

/* 步骤链投影（随迁移保留）。数据源勘误（架构审阅）：只读
 * quest_info["steps"] 运行态（build_step_state 已内联 desc/count/current/completed），
 * 零 def join、单一真源；未接取（AVAILABLE）时 quest_info 为空 → 空数组，
 * provider 可接预览不扩。显式白名单三字段，禁止 copy 直出。 */
private mixed *query_quest_steps_payload(mapping quest_info)
{
    mixed *states;
    mixed *payload;
    int i;
    int n;

    payload = ({ });
    states = mapp(quest_info) && arrayp(quest_info["steps"]) ? quest_info["steps"] : ({ });
    n = sizeof(states);
    if (n > 12)
        n = 12;
    for (i = 0; i < n; i++) {
        string desc;
        int total;
        int done;

        if (!mapp(states[i]))
            continue;
        desc = stringp(states[i]["desc"]) ? states[i]["desc"] : "";
        if (desc == "")
            continue;
        total = intp(states[i]["count"]) && states[i]["count"] > 0 ? states[i]["count"] : 1;
        done = intp(states[i]["current"]) && states[i]["current"] >= 0 ? states[i]["current"] : 0;
        if (done > total)
            done = total;
        payload += ({ ([
            "desc": desc,
            "progress": done,
            "total": total,
        ]) });
    }
    return payload;
}

mapping build_quest_entry_payload(object me, string quest_id)
{
    mapping quest_def;
    mapping quest_info;
    mapping next_step;
    string objective_id;
    int status;
    int progress;
    int step_index;

    if (!objectp(me) || !interactive(me)
        || !stringp(quest_id) || quest_id == "")
        return 0;

    quest_def = QUEST_D->query_visible_quest_definition(me, quest_id);
    if (!mapp(quest_def))
        return 0;

    quest_info = QUEST_D->query_player_quest_runtime(me, quest_id);
    status = QUEST_D->get_quest_status(me, quest_id);
    progress = mapp(quest_info) && intp(quest_info["progress"])
        ? quest_info["progress"]
        : 0;
    next_step = query_quest_next_step_payload(me, quest_id, quest_def, quest_info);
    step_index = query_current_quest_step_index(quest_info);
    objective_id = status == QUEST_STATUS_COMPLETED
        ? quest_id + ":turn_in"
        : sprintf("%s:step:%d", quest_id, step_index < 0 ? 0 : step_index);

    return ([
        "source_kind": "quest",
        "objective_id": objective_id,
        "acceptance_id": mapp(quest_info)
            ? (quest_info["acceptance_id"] || "") : "",
        "acceptance_revision": mapp(quest_info)
            ? (quest_info["acceptance_revision"] || 0) : 0,
        "auto_track": undefinedp(quest_def["auto_track"])
            ? 1 : (quest_def["auto_track"] ? 1 : 0),
        "tracking_priority": intp(quest_def["tracking_priority"])
            ? quest_def["tracking_priority"] : 50,
        "name": stringp(quest_def["name"]) && quest_def["name"] != ""
            ? quest_def["name"]
            : "任务",
        "status": query_quest_status_label(status),
        "progress": progress,
        "total": resolve_quest_total(quest_def, quest_info),
        "provider_id": stringp(quest_def["provider"]) ? quest_def["provider"] : "",
        "provider_name": query_quest_provider_name(quest_def),
        "summary": query_quest_summary(quest_def, quest_info),
        "steps": query_quest_steps_payload(quest_info),
        "rewards": query_quest_rewards_payload(quest_def),
        "can_turn_in": status == QUEST_STATUS_COMPLETED,
    ]) + next_step;
}

varargs int xk_send_quest_list(object me, mapping response_context)
{
    mapping projection;
    mapping payload;
    mapping player_data;
    mixed *cached;
    int content_gen;
    int domain_rev;

    if (!objectp(me) || !interactive(me))
        return 0;

    projection = LOGIN_PROJECTION_D->query_projection_identity(me);
    player_data = QUEST_D->ensure_player_data(me);
    domain_rev = mapp(player_data) ? (player_data["domain_revision"] || 0) : 0;
    content_gen = CONTENT_REGISTRY_D->query_content_generation();
    cached = quest_list_cache_lookup(me, content_gen, domain_rev,
        projection["projection_revision"] || 0,
        query_player_room_path(me));
    if (arrayp(cached) && mapp(cached[4])) {
        // 命中路径防御复制 quests 层（缓存体不可变契约）
        payload = copy(cached[4]);
    } else {
        payload = build_quest_list_payload(me);
        quest_list_cache_store(me, content_gen, domain_rev,
            projection["projection_revision"] || 0,
            query_player_room_path(me), payload);
    }

    // publish-time live-token recheck：缓存复用路径下目标可能已掉线
    if (!objectp(me) || !interactive(me))
        return 0;
    return call_other(XK_PROTOCOL_CORE, "xk_send", me, XK_QUEST, ([
        "protocol_version": XK_PROTOCOL_VERSION_STRING,
        "projection_id": projection["projection_id"] || "",
        "projection_revision": projection["projection_revision"] || 0,
        "projection_generation": projection["projection_generation"] || 0,
        "domain_revision": domain_rev,
        "quests": payload,
    ]), response_context);
}

/** 列表 payload 构建（读模型 miss 路径）；供发送器与合同测试复用。 */
mapping build_quest_list_payload(object me)
{
    mapping payload;
    mapping quests;
    mapping entry;
    string *quest_ids;
    string quest_id;

    payload = ([]);
    quests = QUEST_D->query_player_quests_by_status(me, "active");
    if (!mapp(quests) || sizeof(quests) == 0)
        return payload;
    quest_ids = sort_array(keys(quests), 1);
    foreach (quest_id in quest_ids) {
        entry = build_quest_entry_payload(me, quest_id);
        if (mapp(entry))
            payload[quest_id] = entry;
    }
    return payload;
}

mapping xk_query_quest_objective_projection(object me, string objective_id,
    string acceptance_id, string target_room)
{
    mapping entry;
    mapping projection;
    mapping quests;
    mapping cached_list;
    string quest_id;

    if (!objectp(me) || !interactive(me)) {
        return ([ "ok": 0, "error": "quest_projection_unavailable" ]);
    }
    quests = QUEST_D->query_player_quests_by_status(me, "active");
    // B2：一次重验取读模型快照，循环内仅索引条目（避免逐条目重复校验）
    cached_list = quest_read_model_validated(me);
    foreach (quest_id in sort_array(keys(quests || ([])), 1)) {
        entry = 0;
        if (mapp(cached_list) && mapp(cached_list[quest_id]))
            entry = cached_list[quest_id];
        if (!mapp(entry))
            entry = build_quest_entry_payload(me, quest_id);
        if (!mapp(entry)
            || entry["objective_id"] != objective_id
            || entry["acceptance_id"] != acceptance_id)
            continue;
        if (stringp(target_room) && target_room != ""
            && entry["target_room_id"] != target_room)
            continue;
        projection = LOGIN_PROJECTION_D->query_projection_identity(me);
        return ([
            "ok": 1,
            "objective": copy(entry),
            "projection_id": projection["projection_id"] || "",
            "projection_revision": projection["projection_revision"] || 0,
            "projection_generation": projection["projection_generation"] || 0,
        ]);
    }
    return ([ "ok": 0, "error": "objective_stale" ]);
}

varargs void xk_send_quest_update(object me, string quest_id,
    string operation, int domain_revision, string delta_id,
    mapping response_context)
{
    mapping entry;
    mapping payload;
    mapping projection;

    if (!objectp(me) || !interactive(me) || !stringp(quest_id) || quest_id == "")
        return;

    operation = operation == "remove" ? "remove" : "upsert";
    projection = LOGIN_PROJECTION_D->query_projection_identity(me);
    entry = build_quest_entry_payload(me, quest_id);
    if (operation == "upsert" && !mapp(entry))
        return;
    payload = ([
        "protocol_version": XK_PROTOCOL_VERSION_STRING,
        "projection_id": projection["projection_id"] || "",
        "projection_revision": projection["projection_revision"] || 0,
        "projection_generation": projection["projection_generation"] || 0,
        "domain_revision": domain_revision >= 0 ? domain_revision : 0,
        "delta_id": stringp(delta_id) ? delta_id : "",
        "operation": operation,
        "quest_id": quest_id,
    ]);
    if (operation == "remove") {
        call_other(XK_PROTOCOL_CORE, "xk_send", me, XK_QUEST_UPDATE, payload,
            response_context);
        return;
    }
    payload += ([
        "source_kind": entry["source_kind"],
        "objective_id": entry["objective_id"],
        "acceptance_id": entry["acceptance_id"],
        "acceptance_revision": entry["acceptance_revision"],
        "auto_track": entry["auto_track"],
        "tracking_priority": entry["tracking_priority"],
        "name": entry["name"],
        "status": entry["status"],
        "progress": entry["progress"],
        "total": entry["total"],
        "provider_id": entry["provider_id"],
        "provider_name": entry["provider_name"],
        "summary": entry["summary"],
        "steps": entry["steps"] || ({ }),
        "rewards": entry["rewards"],
        "can_turn_in": entry["can_turn_in"],
        "next_step_label": entry["next_step_label"] || "",
        "target_room_id": entry["target_room_id"] || "",
        "target_room_name": entry["target_room_name"] || "",
        "target_entity_id": entry["target_entity_id"] || "",
        "target_entity_name": entry["target_entity_name"] || "",
        "recommended_command": entry["recommended_command"] || "",
        "can_navigate": entry["can_navigate"] || 0,
    ]);
    call_other(XK_PROTOCOL_CORE, "xk_send", me, XK_QUEST_UPDATE, payload,
        response_context);
}
