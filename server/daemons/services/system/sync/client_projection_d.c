/**
 * @file    client_projection_d.c
 * @brief   客户端协议投影服务



 *
 * @par Description:
 * 统一负责把服务端运行时对象/模板事实投影为客户端可消费 payload。
 * 这里先收口物品运行时视图、模板物品视图、房间/背包列表物品视图
 * 和商店商品视图，
 * 后续房间/对话投影也应优先继续落在这一层，而不是散落在 feedback
 * 或 simul_efun helper 里各自拼字段。
 */

#pragma strict_types
#pragma save_types

#include <game/quest.h>
#include <system/services.h>

#define XK_PROTOCOL_CORE "/kernel/framework/xk_protocol_core"

inherit "/lib/framework/daemon";
inherit "/lib/std/utils/util_owner_projection";
inherit "/lib/std/utils/util_status";

nosave private mapping _hp_dialog_preencoded_template;
nosave private mapping _score_dialog_preencoded_template;

private string *append_unique_string(string *values, string value)
{
    if (!arrayp(values))
        values = ({ });
    if (stringp(value) && value != "" && member_array(value, values) == -1)
        values += ({ value });
    return values;
}

private string compact_projection_line(string text)
{
    string *parts;
    string *words;
    string part;
    int word_count;

    if (!stringp(text) || text == "")
        return "";

    text = replace_string(text, "\t", " ");
    parts = explode(text, " ");
    words = allocate(sizeof(parts));
    word_count = 0;
    foreach (part in parts) {
        if (stringp(part) && part != "")
            words[word_count++] = part;
    }

    return word_count > 0 ? implode(words[0..word_count - 1], " ") : "";
}

private string normalize_projection_text(string text)
{
    string *lines;
    string *normalized_lines;
    string line;
    int line_count;

    if (!stringp(text) || text == "")
        return "";

    text = replace_string(text, "\r", "");
    lines = explode(text, "\n");
    normalized_lines = allocate(sizeof(lines));
    line_count = 0;
    foreach (line in lines) {
        line = compact_projection_line(line);
        if (stringp(line) && line != "")
            normalized_lines[line_count++] = line;
    }

    return line_count > 0 ? implode(normalized_lines[0..line_count - 1], " ") : "";
}

private string query_summary_from_normalized_line(string normalized)
{
    string sentence;
    string remainder;

    if (!stringp(normalized) || normalized == "")
        return "";
    if (sscanf(normalized, "%s。%s", sentence, remainder) == 2
        && strlen(sentence) <= 32)
        return sentence + "。";
    if (strlen(normalized) > 36)
        return normalized[0..35] + "…";
    return normalized;
}

private string query_item_type_label(string item_type, string consume_action)
{
    switch (item_type) {
    case "consumable":
        return consume_action == "drink" ? "饮品" : "药品";
    case "weapon":
        return "武器";
    case "armor":
        return "装备";
    case "scene":
    case "scenery":
    case "prop":
    case "object":
    case "furniture":
    case "environment":
        return "场景";
    default:
        return "物品";
    }
}

private string query_npc_type_label(object npc, object player)
{
    mapping definitions;
    mapping definition;
    string provider_id;
    string npc_type;
    string shop_id;
    string quest_id;

    if (!objectp(npc))
        return "人物";

    provider_id = function_exists("query_quest_provider_id", npc)
        ? npc->query_quest_provider_id()
        : "";
    shop_id = npc->query("shop_id") || "";
    npc_type = npc->query("npc_type") || "";
    if ((stringp(shop_id) && shop_id != "")
        || npc_type == "vendor")
        return "商人";
    if (stringp(provider_id) && provider_id != "" && objectp(player)) {
        definitions = QUEST_D->query_visible_quest_definitions(player);
        foreach (quest_id, definition in definitions)
            if (mapp(definition) && definition["provider"] == provider_id)
                return "任务";
    }
    return "人物";
}

private string query_npc_summary_from_description(string description, string type_label)
{
    description = query_summary_from_normalized_line(description);
    if (stringp(description) && description != "")
        return description;

    switch (type_label) {
    case "商人":
        return "可交易人物";
    case "任务":
        return "可交谈并承接委托";
    default:
        return "可交谈人物";
    }
}

private mapping build_named_entity_payload(object ob)
{
    string object_id;
    string object_name;
    mixed object_id_raw;
    mixed object_name_raw;

    if (!objectp(ob))
        return 0;

    object_id_raw = call_other("/kernel/simul_efun", "get_object_id", ob);
    object_name_raw = ob->name() || ob->query("name") || "";
    if (arrayp(object_id_raw) && sizeof(object_id_raw) > 0)
        object_id = object_id_raw[0];
    else if (stringp(object_id_raw))
        object_id = object_id_raw;
    else
        object_id = file_name(ob);

    if (arrayp(object_name_raw) && sizeof(object_name_raw) > 0)
        object_name = object_name_raw[0];
    else if (stringp(object_name_raw))
        object_name = object_name_raw;
    else
        object_name = "";

    return ([
        "id": object_id,
        "name": object_name,
    ]);
}

private mapping build_client_action(string label, string cmd, string kind,
    string surface, string role, string target_group)
{
    mapping action;

    if (!stringp(label) || label == "" || !stringp(cmd) || cmd == "")
        return 0;

    action = ([
        "label": label,
        "cmd": cmd,
    ]);
    if (stringp(kind) && kind != "")
        action["kind"] = kind;
    if (stringp(surface) && surface != "")
        action["surface"] = surface;
    if (stringp(role) && role != "")
        action["role"] = role;
    if (stringp(target_group) && target_group != "")
        action["target_group"] = target_group;
    return action;
}

private mapping *slice_client_action_buffer(mapping *actions, int action_count)
{
    if (!arrayp(actions) || action_count <= 0)
        return ({ });
    if (action_count >= sizeof(actions))
        return actions;
    return actions[0..action_count - 1];
}

private varargs int store_client_action(mapping *actions, int action_count, string label,
    string cmd, string kind, string surface, string role, string target_group,
    mixed guided)
{
    mapping action;
    mapping existing;
    int index;

    if (!arrayp(actions) || action_count < 0)
        return action_count;
    if (action_count >= sizeof(actions)) {
        /* L-39：容量按实际动态动作数构造后不应满员；一旦满员必须可观测，
         * 不允许静默删尾（后加入的任务/交易入口被吃掉且无溢出结果）。 */
        log_file("system/projection", sprintf(
            "[%s] client action buffer overflow: capacity=%d label=%s cmd=%s\n",
            ctime(time()), sizeof(actions), label, cmd));
        return action_count;
    }

    action = build_client_action(label, cmd, kind, surface, role, target_group);
    if (!mapp(action))
        return action_count;
    /* guided 透传（房间投影面防剥标，缺省不写键）。 */
    if (!nullp(guided) && guided)
        action["guided"] = 1;

    for (index = 0; index < action_count; index++) {
        existing = actions[index];
        if (mapp(existing) && existing["cmd"] == action["cmd"]) {
            /* L-38：同命令去重不得丢引导属性——先写入的普通动作为
             * 同命令 guided 动作时合并标记（指引与到达顺序无关）。 */
            if (action["guided"] && !existing["guided"])
                existing["guided"] = 1;
            return action_count;
        }
    }
    actions[action_count] = action;
    return action_count + 1;
}

private mapping *append_client_ask_topic_actions(mapping *actions, object npc,
    object player, string target)
{
    mapping *action_buffer;
    mixed *topics;
    mapping topic_def;
    string label;
    string topic;
    int action_count;
    int index;

    actions = arrayp(actions) ? actions : ({ });
    if (!objectp(npc) || !stringp(target) || target == "")
        return actions;
    if (!function_exists("query_client_ask_topics", npc))
        return actions;

    topics = npc->query_client_ask_topics(player);
    if (!arrayp(topics) || sizeof(topics) == 0)
        return actions;

    action_buffer = allocate(sizeof(actions) + sizeof(topics));
    for (index = 0; index < sizeof(actions); index++)
        action_buffer[index] = actions[index];
    action_count = sizeof(actions);

    foreach (topic_def in topics) {
        if (!mapp(topic_def))
            continue;
        topic = topic_def["topic"] || "";
        label = topic_def["label"] || "";
        if (topic == "")
            continue;
        if (label == "")
            label = topic;   /* 话题具名化，与 npc.c 侧默认一致 */
        action_count = store_client_action(action_buffer, action_count, label,
            "ask " + target + " about " + topic, "ask", "conversation",
            "secondary", "conversation");
    }
    return slice_client_action_buffer(action_buffer, action_count);
}

private mapping build_client_action_group(string id, string title, string surface,
    mapping *actions)
{
    if (!arrayp(actions) || sizeof(actions) == 0)
        return 0;
    return ([
        "id": id,
        "title": title,
        "surface": surface,
        "actions": actions,
    ]);
}

private string *append_room_item_match_ids(string *ids, mapping seen, mixed value)
{
    mixed candidate;

    if (stringp(value)) {
        if (value != "" && undefinedp(seen[value])) {
            seen[value] = 1;
            ids += ({ value });
        }
        return ids;
    }
    if (!arrayp(value))
        return ids;

    foreach (candidate in value) {
        if (!stringp(candidate) || candidate == ""
            || !undefinedp(seen[candidate]))
            continue;
        seen[candidate] = 1;
        ids += ({ candidate });
    }
    return ids;
}

private string *query_room_item_match_ids(object item, mapping payload)
{
    mapping seen;
    mixed raw;
    string *ids;

    ids = ({ });
    seen = ([ ]);
    if (mapp(payload) && stringp(payload["id"]))
        ids = append_room_item_match_ids(ids, seen, payload["id"]);
    if (!objectp(item))
        return ids;

    if (function_exists("query_temp", item))
        ids = append_room_item_match_ids(ids, seen,
            item->query_temp("world_lifecycle/content_entity_id"));
    raw = call_other("/kernel/simul_efun", "get_object_id", item);
    ids = append_room_item_match_ids(ids, seen, raw);
    if (function_exists("query_id", item))
        ids = append_room_item_match_ids(ids, seen, item->query_id());
    ids = append_room_item_match_ids(ids, seen, item->query("id"));
    ids = append_room_item_match_ids(ids, seen, item->query("template_id"));
    ids = append_room_item_match_ids(ids, seen, item->query("name"));
    return ids;
}

private mapping *build_room_item_action_groups(object item, object player, mapping payload)
{
    mapping *root_actions;
    mapping guidance;
    mixed *guidance_actions;
    mixed usage_modes;
    string *match_ids;
    string target;
    string listen_target;
    int can_gather;
    int root_count;

    if (!objectp(item) || !objectp(player))
        return ({ });

    match_ids = query_room_item_match_ids(item, payload);
    if (sizeof(match_ids) == 0)
        return ({ });
    target = match_ids[0];
    listen_target = objectp(item) && stringp(item->query("listen_point_id"))
        ? item->query("listen_point_id") : target;

    guidance_actions = QUEST_D->query_provider_step_actions(player, match_ids);
    root_actions = allocate(5 + sizeof(guidance_actions));
    root_count = 0;
    foreach (guidance in guidance_actions) {
        if (!mapp(guidance) || !stringp(guidance["cmd"])
            || guidance["cmd"] == "")
            continue;
        root_count = store_client_action(root_actions, root_count,
            guidance["label"] || "处理", guidance["cmd"],
            guidance["kind"] || "quest", guidance["surface"] || "root",
            guidance["role"] || "primary", guidance["target_group"] || "quest",
            guidance["guided"]);
    }

    usage_modes = mapp(payload) && arrayp(payload["usage_modes"])
        ? payload["usage_modes"] : ({ });
    if (!arrayp(usage_modes) || sizeof(usage_modes) == 0)
        usage_modes = item->query("usage_modes");
    if (!arrayp(usage_modes) || sizeof(usage_modes) == 0)
        usage_modes = item->query("acquisition/usage_modes") || ({ });
    if (arrayp(usage_modes) && member_array("look", usage_modes) != -1)
        root_count = store_client_action(root_actions, root_count, "查看",
            "look " + target, "look", "root", "secondary", "root");
    if (arrayp(usage_modes) && member_array("listen", usage_modes) != -1)
        root_count = store_client_action(root_actions, root_count, "偷听",
            "listen " + listen_target, "listen", "root", "primary", "root");
    can_gather = stringp(function_exists("query_gather_target", item))
        || stringp(function_exists("query_gather_contract", item))
        || mapp(item->query("gather_target"))
        || mapp(item->query("gather_contract"));
    if (can_gather)
        root_count = store_client_action(root_actions, root_count, "采集",
            "gather " + target, "gather", "root", "primary", "root");
    if (item->query("exploration_chest_id") || payload["type"] == "exploration_chest")
        root_count = store_client_action(root_actions, root_count, "开启",
            "open " + target, "open", "root", "primary", "root");
    if (!item->query("no_get"))
        root_count = store_client_action(root_actions, root_count, "拾取",
            "get " + target, "get", "root", "secondary", "root");

    root_actions = slice_client_action_buffer(root_actions, root_count);
    return root_count > 0 ? ({ build_client_action_group("root", "主要操作", "root", root_actions) }) : ({ });
}

private string query_npc_command_target(object npc, mapping payload)
{
    string target;

    target = mapp(payload) && stringp(payload["id"]) ? payload["id"] : "";
    if (target != "")
        return target;
    return objectp(npc) ? get_object_id(npc) : "";
}

string *query_available_provider_quest_ids(object player, string provider_id)
{
    mapping defs;
    string *quest_ids;
    string *available;
    string quest_id;

    if (!objectp(player)
        || !stringp(provider_id) || provider_id == "")
        return ({ });

    defs = QUEST_D->query_visible_quests_by_provider(player, provider_id) || ([]);
    quest_ids = sort_array(keys(defs), 1);
    available = ({ });
    foreach (quest_id in quest_ids) {
        if (QUEST_D->get_quest_status(player, quest_id) != QUEST_STATUS_AVAILABLE)
            continue;
        if (!QUEST_D->is_quest_visible_for_player(player, quest_id))
            continue;
        available += ({ quest_id });
    }
    return available;
}

private string *query_actionable_provider_quest_ids(object player, string provider_id);

string *query_provider_marker_quest_ids(object player, string provider_id)
{
    if (!stringp(provider_id) || provider_id == "")
        return ({ });

    return query_actionable_provider_quest_ids(player, provider_id);
}

string *query_provider_marker_only_quest_ids(object player, string provider_id)
{
    mapping defs;
    mapping quest_def;
    string *published;
    string *quest_ids;
    string quest_id;

    if (!objectp(player)
        || !stringp(provider_id) || provider_id == "")
        return ({ });

    defs = QUEST_D->query_visible_quests_by_provider(player, provider_id) || ([]);
    quest_ids = sort_array(keys(defs), 1);
    published = ({ });
    foreach (quest_id in quest_ids) {
        quest_def = defs[quest_id];
        if (!mapp(quest_def))
            continue;
        if (member_array("released", keys(quest_def)) != -1 && quest_def["released"] == 0)
            continue;
        if (!QUEST_D->is_quest_visible_for_player(player, quest_id))
            continue;
        if (QUEST_D->get_quest_status(player, quest_id) == QUEST_STATUS_DELIVERED)
            continue;
        published = append_unique_string(published, quest_id);
    }
    return published;
}

private string *query_actionable_provider_quest_ids(object player, string provider_id)
{
    mapping quest_ids_by_provider;

    if (!objectp(player) || !stringp(provider_id) || provider_id == "")
        return ({ });

    quest_ids_by_provider = QUEST_D->query_map_marker_quest_ids_by_provider(player);
    return arrayp(quest_ids_by_provider[provider_id])
        ? quest_ids_by_provider[provider_id] + ({ })
        : ({ });
}

private mapping *build_provider_quest_actions(object player, string provider_id)
{
    mapping defs;
    mapping quest_info;
    mapping *actions;
    string *quest_ids;
    string quest_id;
    string quest_name;
    int status;
    int action_count;

    if (!objectp(player)
        || !stringp(provider_id) || provider_id == "")
        return ({ });

    defs = QUEST_D->query_visible_quests_by_provider(player, provider_id) || ([]);
    quest_ids = sort_array(keys(defs), 1);
    actions = allocate(sizeof(quest_ids) * 3);
    action_count = 0;
    foreach (quest_id in quest_ids) {
        status = QUEST_D->get_quest_status(player, quest_id);
        if (status == QUEST_STATUS_DELIVERED)
            continue;
        if (status == QUEST_STATUS_AVAILABLE
            && !QUEST_D->can_accept_quest(player, quest_id))
            continue;
        if (status == QUEST_STATUS_FAILED) {
            quest_info = QUEST_D->query_player_quest_runtime(player, quest_id);
            if (!mapp(quest_info) || !quest_info["recoverable"])
                continue;
        }
        quest_name = mapp(defs[quest_id]) && stringp(defs[quest_id]["name"]) && defs[quest_id]["name"] != ""
            ? defs[quest_id]["name"]
            : "任务";
        action_count = store_client_action(actions, action_count, "详情 " + quest_name,
            "quest info " + quest_id, "quest_info", "quest", "secondary", "quest");
        switch (status) {
        case QUEST_STATUS_COMPLETED:
            action_count = store_client_action(actions, action_count, "交付 " + quest_name,
                "quest complete " + quest_id, "quest_complete", "quest", "primary", "quest");
            break;
        case QUEST_STATUS_FAILED:
            quest_info = QUEST_D->query_player_quest_runtime(player, quest_id);
            if (mapp(quest_info) && quest_info["recoverable"])
                action_count = store_client_action(actions, action_count, "恢复 " + quest_name,
                    "quest recover " + quest_id, "quest_recover", "quest", "secondary", "quest");
            break;
        default:
            if (QUEST_D->can_accept_quest(player, quest_id))
                action_count = store_client_action(actions, action_count, "接取 " + quest_name,
                    "quest accept " + quest_id, "quest_accept", "quest", "primary", "quest");
        }
    }
    return slice_client_action_buffer(actions, action_count);
}

private mapping *build_room_npc_action_groups(object npc, object player, mapping payload)
{
    mapping *groups;
    mapping *root_actions;
    mapping *conversation_actions;
    mapping *quest_actions;
    mapping *trade_actions;
    mapping group;
    string target;
    string provider_id;
    string shop_id;
    string npc_type;
    string talk_entry_label;
    int supports_learning;
    int stealable;
    int trade_proxy;
    int group_count;
    int root_count;
    int conversation_count;
    int trade_count;
    mapping *resident_actions;
    mixed *guidance_actions;
    string *guidance_match_ids;
    mixed guidance_id_value;
    mixed resident_value;

    groups = allocate(4);
    group_count = 0;
    if (!objectp(npc))
        return ({ });

    target = query_npc_command_target(npc, payload);
    provider_id = function_exists("query_quest_provider_id", npc) ? npc->query_quest_provider_id() : "";
    shop_id = npc->query("shop_id") || "";
    npc_type = npc->query("npc_type") || "";
    supports_learning = function_exists("can_teach_skills", npc)
        && npc->can_teach_skills(player) ? 1 : 0;
    stealable = npc->query("stealable") == 1;
    trade_proxy = npc->query("trade_proxy") == 1;

    /* L-39：动态动作先取齐再按实际数量构造容器——固定槽（曾 10→16）在
     * 「常驻设施动作 + 任务指引动作」叠加时会静默截断尾部（后加入的
     * 任务/交易入口被吃掉且无溢出结果）。容量 = 固定动作上限(10) +
     * 常驻动作数 + 指引动作数，与房间物品动作同一构造模式。 */
    resident_actions = ({ });
    if (function_exists("query_resident_root_actions", npc)) {
        resident_value = npc->query_resident_root_actions();
        if (arrayp(resident_value))
            resident_actions = resident_value;
    }
    guidance_actions = ({ });
    quest_actions = ({ });
    if (stringp(provider_id) && provider_id != "")
        quest_actions = build_provider_quest_actions(player, provider_id);
    if (sizeof(quest_actions) > 0)
        resident_actions += ({ ([
            "label": "任务",
            "cmd": "quest " + provider_id,
            "kind": "quest",
            "surface": "root",
            "role": "secondary",
            "target_group": "quest",
        ]) });
    if (stringp(shop_id) && shop_id != "")
        resident_actions += ({ ([
            "label": "货单",
            "cmd": "list",
            "kind": "trade",
            "surface": "root",
            "role": "primary",
            "target_group": "trade",
        ]) });
    if (target != "") {
        /* id 数组真键是 set("id",...)（"ids" 为死键），
         * 别名/模板 id 由此进入匹配集。 */
        guidance_id_value = function_exists("query_id", npc)
            ? npc->query_id() : get_object_id(npc);
        guidance_match_ids = arrayp(guidance_id_value)
            ? guidance_id_value
            : stringp(guidance_id_value) ? ({ guidance_id_value }) : ({ });
        guidance_actions = QUEST_D->query_provider_step_actions(player,
            guidance_match_ids + ({ target, provider_id }));
        if (!arrayp(guidance_actions))
            guidance_actions = ({ });
    }
    root_actions = allocate(10 + sizeof(resident_actions)
        + sizeof(guidance_actions));
    conversation_actions = allocate(1);
    trade_actions = allocate(3);
    root_count = 0;
    conversation_count = 0;
    trade_count = 0;

    if (target != "") {
        root_count = store_client_action(root_actions, root_count, "查看", "look " + target, "look", "root", "secondary", "root");
        if (stealable)
            root_count = store_client_action(root_actions, root_count, "偷取",
                "steal " + target, "steal", "root", "danger", "root");
        if (trade_proxy) {
            root_count = store_client_action(root_actions, root_count, "交易",
                "trade request " + target, "trade", "root", "primary", "trade");
            trade_count = store_client_action(trade_actions, trade_count, "交易",
                "trade request " + target, "trade", "trade", "primary", "trade");
        }
        /* 无话题业务 NPC 的具名功能入口（覆写 query_talk_entry_label 才显）。 */
        if (function_exists("query_talk_entry_label", npc)) {
            talk_entry_label = npc->query_talk_entry_label();
            if (talk_entry_label != "")
                root_count = store_client_action(root_actions, root_count, talk_entry_label, "talk " + target, "talk", "root", "primary", "root");
        }
        root_count = store_client_action(root_actions, root_count, "给予", "give " + target, "give", "root", "secondary", "give");
        if (npc_type == "banker" || npc->query("banker"))
            root_count = store_client_action(root_actions, root_count, "银行", "bank", "bank", "root", "primary", "root");
        if (npc_type == "healer" || npc->query("healer"))
            root_count = store_client_action(root_actions, root_count, "疗伤", "heal", "heal", "root", "primary", "root");
        if (supports_learning)
            root_count = store_client_action(root_actions, root_count, "技能", "learn list " + target, "learn", "root", "secondary", "learn");
        conversation_actions = slice_client_action_buffer(conversation_actions, conversation_count);
        conversation_actions = append_client_ask_topic_actions(conversation_actions,
            npc, player, target);
        if (!npc->query("no_fight") || function_exists("do_fight", npc))
            root_count = store_client_action(root_actions, root_count, "切磋", "fight " + target, "fight", "root", "secondary", "root");
        if (!npc->query("no_fight"))
            root_count = store_client_action(root_actions, root_count, "攻击", "kill " + target, "attack", "root", "danger", "root");
        /* 设施常驻动作并入（数量已计入容量）。 */
        foreach (mapping ra in resident_actions)
            if (mapp(ra) && stringp(ra["label"]) && stringp(ra["cmd"]))
                root_count = store_client_action(root_actions, root_count,
                    ra["label"], ra["cmd"], ra["kind"] || "root",
                    ra["surface"] || "root", ra["role"] || "primary",
                    ra["target_group"] || "root");
        /* 任务指引动作并入（数量已计入容量；进度感知，QUEST_D 单一权威
         * 防御消费，match_ids 与弹窗侧同款：可解析 id 全集 + provider/target）。 */
        foreach (mapping ga in guidance_actions)
            if (mapp(ga) && stringp(ga["label"]) && stringp(ga["cmd"]))
                root_count = store_client_action(root_actions, root_count,
                    ga["label"], ga["cmd"], ga["kind"] || "quest",
                    ga["surface"] || "root", ga["role"] || "primary",
                    ga["target_group"] || "quest",
                    ga["guided"] /* guided 透传防剥标 */);
    }

    if (stringp(shop_id) && shop_id != "")
        trade_count = store_client_action(trade_actions, trade_count, "货单", "list", "trade", "trade", "primary", "trade");

    root_actions = slice_client_action_buffer(root_actions, root_count);
    trade_actions = slice_client_action_buffer(trade_actions, trade_count);
    foreach (group in ({
        build_client_action_group("root", "主要操作", "root", root_actions),
        build_client_action_group("conversation", "话题", "conversation", conversation_actions),
        build_client_action_group("quest", "任务", "quest", quest_actions),
        build_client_action_group("trade", "交易", "trade", trade_actions),
    })) {
        if (!mapp(group))
            continue;
        group["actions"] = CONTENT_MODE_POLICY_D->project_actions(player,
            group["actions"]);
        if (sizeof(group["actions"]) > 0)
            groups[group_count++] = group;
    }
    return group_count > 0 ? groups[0..group_count - 1] : ({ });
}

private mapping build_item_payload_core(object item, mapping result)
{
    mapping payload;
    string item_type;
    string sub_type;

    result = mapp(result) ? result : ([]);

    item_type = objectp(item) ? (item->query("item_type") || "misc") : "misc";
    sub_type = objectp(item) ? (item->query("sub_type") || item->query("armor_type") || "") : "";

    payload = ([
        "item_id": result["item_id"] || "",
        "template_id": result["template_id"] || "",
        "instance_id": result["instance_id"] || "",
        "owner_id": result["owner_id"] || "",
        "binding_policy": result["binding_policy"] || "",
        "provenance": mapp(result["provenance"])
            ? copy(result["provenance"]) : ([]),
        "usage_modes": arrayp(result["usage_modes"])
            ? copy(result["usage_modes"]) : ({ }),
        "type": result["type"] || "misc",
        "sub_type": result["sub_type"] || "",
        "quality": result["quality"] || 1,
        "required_skill_cap": result["required_skill_cap"] || 0,
        "durability": result["durability"] || 0,
        "max_durability": result["max_durability"] || 0,
        "equipped": result["equipped"] || 0,
        "enhance_level": result["enhance_level"] || 0,
    ]);

    if (!objectp(item))
        return payload;

    if (payload["item_id"] == "")
        payload["item_id"] = item->query("template_id") || "";
    if (payload["template_id"] == "")
        payload["template_id"] = item->query("template_id") || "";
    if (payload["instance_id"] == "")
        payload["instance_id"] = item->query("instance_id") || "";
    if (payload["owner_id"] == "")
        payload["owner_id"] = item->query("owner_id") || "";
    if (payload["binding_policy"] == "")
        payload["binding_policy"] = item->query("binding_policy")
            || item->query("acquisition/binding_policy") || "";
    if (!mapp(payload["provenance"]) || sizeof(payload["provenance"]) == 0)
        payload["provenance"] = item->query("acquisition/provenance") || ([]);
    if (!arrayp(payload["usage_modes"]) || sizeof(payload["usage_modes"]) == 0)
        payload["usage_modes"] = item->query("acquisition/usage_modes") || ({ });
    if (payload["type"] == "misc")
        payload["type"] = item_type;
    if (payload["sub_type"] == "")
        payload["sub_type"] = sub_type;
    if (payload["quality"] <= 1)
        payload["quality"] = item->query("quality") || 1;
    if (payload["required_skill_cap"] <= 0)
        payload["required_skill_cap"] = item->query("required_skill_cap") || 0;
    if (payload["durability"] <= 0)
        payload["durability"] = item->query("durability") || 0;
    if (payload["max_durability"] <= 0)
        payload["max_durability"] = item->query("max_durability") || 0;
    if (payload["enhance_level"] <= 0)
        payload["enhance_level"] = item->query("enhance_level") || 0;
    if (!payload["equipped"] && function_exists("query_equipped", item))
        payload["equipped"] = item->query_equipped();

    return payload;
}

mapping build_item_client_payload(object item, mapping result)
{
    mapping payload;
    string item_type;
    string consume_action;

    result = mapp(result) ? result : ([]);
    payload = build_item_payload_core(item, result);
    payload["can_equip"] = result["can_equip"] || 0;
    payload["can_use"] = result["can_use"] || 0;

    if (!objectp(item))
        return payload;

    item_type = item->query("item_type") || "misc";
    if (!payload["can_equip"])
        payload["can_equip"] = item_type == "weapon" || item_type == "armor";
    if (!payload["can_use"])
        payload["can_use"] = item_type == "consumable";
    consume_action = item->query("consume_action") || "";
    payload["consume_action"] = consume_action;
    payload["type_label"] = query_item_type_label(payload["type"], consume_action);

    return payload;
}

varargs mapping build_room_npc_payload(object npc, object player)
{
    mapping payload;
    string type_label;
    string description;
    string provider_id;
    string *available_quest_ids;
    string *marker_quest_ids;

    payload = build_named_entity_payload(npc);
    if (!mapp(payload))
        return 0;

    type_label = query_npc_type_label(npc, player);
    description = normalize_projection_text(
        npc->query("long") || npc->query("description") || "");
    payload["description"] = description;
    payload["summary"] = query_npc_summary_from_description(description, type_label);
    payload["type"] = "npc";
    payload["type_label"] = type_label;
    payload["action_groups"] = build_room_npc_action_groups(npc, player, payload);
    provider_id = function_exists("query_quest_provider_id", npc) ? npc->query_quest_provider_id() : "";
    available_quest_ids = query_actionable_provider_quest_ids(player, provider_id);
    marker_quest_ids = query_provider_marker_only_quest_ids(player, provider_id);
    if (sizeof(available_quest_ids) > 0) {
        payload["quest_available"] = 1;
        payload["available_quest_ids"] = available_quest_ids;
        payload["quest_available_ids"] = available_quest_ids;
    } else if (sizeof(marker_quest_ids) > 0) {
        payload["published_quest_ids"] = marker_quest_ids;
        payload["quest_marker_only"] = 1;
    }
    return payload;
}

mapping build_item_client_payload_from_template_id(string item_id, object item)
{
    mapping requirements;
    mapping result;
    mapping template;
    int can_equip;
    int can_use;
    string item_type;
    string sub_type;

    if (!stringp(item_id) || item_id == "")
        return build_item_client_payload(item, ([]));

    template = ITEM_D->query_item_template(item_id);
    if (!mapp(template))
        template = ([]);

    requirements = mapp(template["requirements"]) ? template["requirements"] : ([]);
    item_type = objectp(item)
        ? (item->query("item_type") || template["type"] || "misc")
        : (template["type"] || "misc");
    sub_type = objectp(item)
        ? (item->query("sub_type") || item->query("armor_type")
            || template["armor_type"] || template["sub_type"] || "")
        : (template["armor_type"] || template["sub_type"] || "");
    can_equip = item_type == "weapon" || item_type == "armor";
    can_use = item_type == "consumable";

    result = ([
        "item_id": item_id,
        "template_id": item_id,
        "instance_id": objectp(item) ? (item->query("instance_id") || "") : "",
        "owner_id": objectp(item) ? (item->query("owner_id") || "") : "",
        "binding_policy": objectp(item)
            ? (item->query("binding_policy")
                || item->query("acquisition/binding_policy") || "")
            : (template["binding_policy"] || ""),
        "provenance": objectp(item)
            ? (item->query("acquisition/provenance") || ([]))
            : (mapp(template["acquisition/provenance"])
                ? template["acquisition/provenance"] : ([])),
        "usage_modes": objectp(item)
            ? (item->query("acquisition/usage_modes") || ({ }))
            : (arrayp(template["usage_modes"]) ? template["usage_modes"] : ({ })),
        "type": item_type,
        "sub_type": sub_type,
        "quality": objectp(item)
            ? (item->query("quality") || (template["quality"] || 1))
            : (template["quality"] || 1),
        "required_skill_cap": objectp(item)
            ? (item->query("required_skill_cap") || (requirements["skill_cap"] || 0))
            : (requirements["skill_cap"] || 0),
        "durability": objectp(item)
            ? (item->query("durability") || 0)
            : (template["durability"] || (template["max_durability"] || 0)),
        "max_durability": objectp(item)
            ? (item->query("max_durability") || 0)
            : (template["max_durability"] || 0),
        "can_equip": can_equip,
        "can_use": can_use,
        "equipped": objectp(item) && function_exists("query_equipped", item)
            && item->query_equipped(),
        "enhance_level": objectp(item) ? (item->query("enhance_level") || 0) : 0,
    ]);

    return build_item_client_payload(item, result);
}

mapping build_shop_item_payload(string item_id, mapping item_data)
{
    mapping payload;
    mapping template;
    string display_name;

    if (!stringp(item_id) || item_id == "" || !mapp(item_data))
        return 0;

    template = ITEM_D->query_item_template(item_id);
    if (!mapp(template))
        template = ([]);
    display_name = stringp(item_data["name"]) && item_data["name"] != ""
        ? item_data["name"]
        : (stringp(template["name"]) && template["name"] != "" ? template["name"] : item_id);

    payload = build_item_client_payload_from_template_id(item_id, 0);
    payload["id"] = item_id;
    payload["name"] = display_name;
    payload["price"] = item_data["price"] || 0;
    payload["price_label"] = MONEY_D->format_money(payload["price"] || 0);
    payload["stock"] = -1;
    map_delete(payload, "item_id");

    return payload;
}

mapping build_item_list_payload(object item, object player)
{
    mapping payload;
    int quantity;

    payload = build_named_entity_payload(item);
    if (!mapp(payload))
        return 0;

    payload += build_item_payload_core(item, ([]));
    map_delete(payload, "item_id");
    quantity = 1;
    if (function_exists("query_amount", item))
        quantity = item->query_amount();
    if (!intp(quantity) || quantity <= 0)
        quantity = 1;

    payload["description"] = normalize_projection_text(item->query("long") || "");
    payload["summary"] = query_summary_from_normalized_line(payload["description"]);
    payload["quantity"] = quantity;
    payload["location"] = "room";
    payload["consume_action"] = item->query("consume_action") || "";
    payload["type_label"] = query_item_type_label(payload["type"], payload["consume_action"]);
    payload["can_use"] = (payload["type"] == "consumable"
        && (function_exists("consume", item)
            || function_exists("do_eat", item)
            || function_exists("do_drink", item))) ? 1 : 0;
    payload["can_equip"] = (payload["type"] == "weapon"
        || payload["type"] == "armor"
        || function_exists("wield", item)
        || function_exists("wear", item)) ? 1 : 0;
    payload["can_give"] = (!payload["equipped"]
        && !item->query("no_give")) ? 1 : 0;
    payload["action_groups"] = build_room_item_action_groups(item, player, payload);

    return payload;
}

mapping query_inventory_item_projection_snapshot(object item)
{
    mapping named;
    mapping payload;
    int quantity;

    named = build_named_entity_payload(item);
    if (!mapp(named) || !objectp(item))
        return 0;
    payload = build_item_payload_core(item, ([]));
    if (!mapp(payload))
        return 0;
    quantity = 1;
    if (function_exists("query_amount", item))
        quantity = item->query_amount();
    if (!intp(quantity) || quantity <= 0)
        quantity = 1;

    payload["id"] = named["id"] || "";
    payload["name"] = named["name"] || "";
    payload["quantity"] = quantity;
    payload["food_remaining"] = item->query("food_remaining") || 0;
    payload["food_supply"] = item->query("food_supply") || 0;
    payload["liquid_remaining"] = item->query("liquid/remaining") || 0;
    payload["liquid_supply"] = item->query("liquid/supply") || 0;
    payload["consume_action"] = item->query("consume_action") || "";
    payload["description_raw"] = item->query("long") || "";
    payload["supports_wield"] = function_exists("wield", item) ? 1 : 0;
    payload["supports_wear"] = function_exists("wear", item) ? 1 : 0;
    payload["no_give"] = item->query("no_give") ? 1 : 0;
    payload["no_drop"] = item->query("no_drop") ? 1 : 0;

    return payload;
}

mapping build_inventory_item_payload(object item)
{
    mapping snapshot;

    snapshot = query_inventory_item_projection_snapshot(item);
    return project_inventory_item_snapshot(snapshot);
}

mapping query_hp_projection_snapshot(object player, string worker_state)
{
    int hp;
    int max_hp;
    int mp;
    int max_mp;

    if (!objectp(player))
        return ([
            "hp": 0,
            "max_hp": 100,
            "mp": 0,
            "max_mp": 100,
            "read_side_worker_state": worker_state || "pending",
        ]);

    hp = function_exists("query_hp", player)
        ? player->query_hp() : (player->query("combat/hp") || 0);
    max_hp = function_exists("query_max_hp", player)
        ? player->query_max_hp() : (player->query("combat/max_hp") || 100);
    mp = function_exists("query_mp", player)
        ? player->query_mp() : (player->query("combat/mp") || 0);
    max_mp = function_exists("query_max_mp", player)
        ? player->query_max_mp() : (player->query("combat/max_mp") || 100);

    return ([
        "hp": hp,
        "max_hp": max_hp,
        "mp": mp,
        "max_mp": max_mp,
        "read_side_worker_state": stringp(worker_state) && worker_state != ""
            ? worker_state : "pending",
    ]);
}

string query_hp_projection_live_token(object player)
{
    int generation;
    int hp;
    int max_hp;
    int mp;
    int max_mp;
    int numeric_generation;
    int skill_generation;

    if (!objectp(player))
        return "invalid";
    generation = function_exists("query_xk_hp_maxima_generation", player)
        ? player->query_xk_hp_maxima_generation() : 0;
    numeric_generation = NUMERIC_RULE_D->query_numeric_config_generation();
    skill_generation = SKILL_D->query_skill_config_generation();
    hp = player->query("combat/hp") || 0;
    max_hp = player->query("combat/max_hp") || 100;
    mp = player->query("combat/mp") || 0;
    max_mp = player->query("combat/max_mp") || 100;
    return sprintf("%d|%d|%d|%d|%d|%d|%d", generation,
        numeric_generation, skill_generation, hp, max_hp, mp, max_mp);
}

private int projection_worker_ready(string worker_state)
{
    return stringp(worker_state)
        && (worker_state == "ready" || worker_state == "ready_cached");
}

private mapping query_hp_dialog_extra(string worker_state)
{
    mapping extra;

    extra = DIALOG_PROJECTION_D->build_dialog_extra("hp", "hp_panel", ([
        "replace_mode": "replace",
        "actions": ({
            (["label":"刷新状态", "cmd":"hp"]),
            (["label":"角色状态", "cmd":"score"]),
            (["label":"服务器状态", "cmd":"status"]),
            (["label":"查看帮助", "cmd":"help hp"]),
        }),
    ]));
    extra["read_side_worker"] = projection_worker_ready(worker_state) ? 1 : 0;
    extra["read_side_worker_state"] = stringp(worker_state)
        && worker_state != "" ? worker_state : "pending";
    return extra;
}

mapping query_hp_dialog_preencoded_template()
{
    mapping dialog_template;

    if (!mapp(_hp_dialog_preencoded_template)) {
        dialog_template = XK_PROTOCOL_CORE->xk_build_dialog_preencoded_template(
            this_object(), "生命状态面板", "",
            query_hp_dialog_extra("ready_cached"));
        if (!mapp(dialog_template) || !mapp(dialog_template["stable_payload"])
            || !mapp(dialog_template["dynamic_defaults"]))
            return 0;
        _hp_dialog_preencoded_template = ([
            "stable_payload": copy(dialog_template["stable_payload"]),
            "dynamic_defaults": copy(dialog_template["dynamic_defaults"]),
        ]);
    }
    return ([
        "stable_payload": copy(_hp_dialog_preencoded_template["stable_payload"]),
        "dynamic_defaults": copy(_hp_dialog_preencoded_template["dynamic_defaults"]),
    ]);
}

private string score_actor_token_state(mapping analysis)
{
    string state;

    state = mapp(analysis) && stringp(analysis["async_state"])
        ? analysis["async_state"] : "";
    switch (state) {
    case "ready":
    case "ready_cached":
    case "failed_cached":
        return "ready";
    case "pending":
    case "pending_cached":
        return "pending";
    default:
        return "unavailable";
    }
}

mapping query_score_projection_snapshot(object player)
{
    mapping analysis;
    mapping combat;
    mapping progression;
    mapping snapshot;
    int actor_started_at;
    int combat_exp;
    int facts_started_at;

    if (!objectp(player))
        return ([]);
    facts_started_at = perf_counter_ns();
    combat = query_combat_resource_snapshot(player);
    combat_exp = player->query("combat_exp") || 0;
    progression = NUMERIC_RULE_D->query_player_progression_snapshot(player);
    snapshot = ([
        "name": call_other("/lib/std/utils/util_player",
            "get_object_display_name", player, "无名侠客"),
        "title": player->query("title") || "",
        "hp": combat["hp"] || 0,
        "max_hp": combat["max_hp"] || 0,
        "mp": combat["mp"] || 0,
        "max_mp": combat["max_mp"] || 0,
        "combat_exp": combat_exp,
        "progression_rank": mapp(progression)
            ? progression["progression_rank"] : 1,
        "skill_cap": mapp(progression) ? progression["skill_cap"] : 0,
        "weapon_name": call_other("/lib/std/utils/util_player",
            "get_object_display_name", player->query_temp("weapon"), "空手"),
        "strength": player->query("attributes/strength") || 10,
        "dexterity": player->query("attributes/dexterity") || 10,
        "constitution": player->query("attributes/constitution") || 10,
        "intelligence": player->query("attributes/intelligence")
            || player->query("int") || 10,
    ]);
    GATEWAY_SESSION_D->record_owner_protocol_projection_timing(
        "score_dialog", "snapshot_facts", perf_counter_ns() - facts_started_at);
    actor_started_at = perf_counter_ns();
    analysis = PLAYER_ACTOR_ANALYSIS_D->query_player_actor_analysis_from_combat(
        player, combat);
    snapshot["actor_analysis"] = mapp(analysis) ? copy(analysis) : ([]);
    GATEWAY_SESSION_D->record_owner_protocol_projection_timing(
        "score_dialog", "snapshot_actor", perf_counter_ns() - actor_started_at);
    return snapshot;
}

string query_score_projection_live_token_from_snapshot(
    object player, mapping snapshot)
{
    mapping analysis;
    int generation;

    if (!objectp(player) || !mapp(snapshot))
        return "invalid";
    generation = player->query_xk_score_projection_generation();
    analysis = mapp(snapshot["actor_analysis"])
        ? snapshot["actor_analysis"] : ([]);
    return sprintf("%d|%d|%d|%d|%d|%d|%s|%d", generation,
        snapshot["hp"] || 0, snapshot["max_hp"] || 0,
        snapshot["mp"] || 0, snapshot["max_mp"] || 0,
        analysis["async_input_hash"] || 0,
        score_actor_token_state(analysis),
        analysis["completed_at"] || 0);
}

string query_score_projection_live_token(object player)
{
    mapping analysis;
    int generation;
    int hp;
    int max_hp;
    int mp;
    int max_mp;

    if (!objectp(player))
        return "invalid";
    generation = player->query_xk_score_projection_generation();
    hp = player->query("combat/hp") || 0;
    max_hp = player->query("combat/max_hp") || 0;
    mp = player->query("combat/mp") || 0;
    max_mp = player->query("combat/max_mp") || 0;
    analysis = PLAYER_ACTOR_ANALYSIS_D->query_player_actor_analysis_token(player);
    return sprintf("%d|%d|%d|%d|%d|%d|%s|%d", generation, hp, max_hp, mp,
        max_mp,
        mapp(analysis) ? (analysis["async_input_hash"] || 0) : 0,
        score_actor_token_state(analysis),
        mapp(analysis) ? (analysis["completed_at"] || 0) : 0);
}

private mapping query_score_dialog_extra(string worker_state)
{
    return DIALOG_PROJECTION_D->build_dialog_extra("score", "score_panel", ([
        "replace_mode": "replace",
        "actions": ({
            (["label":"刷新状态", "cmd":"score"]),
            (["label":"服务器状态", "cmd":"status"]),
            (["label":"查看时间", "cmd":"time"]),
            (["label":"查看帮助", "cmd":"help score"]),
        }),
    ])) + ([
        "read_side_worker": projection_worker_ready(worker_state) ? 1 : 0,
        "read_side_worker_state": worker_state || "pending",
    ]);
}

mapping query_score_dialog_preencoded_template()
{
    mapping dialog_template;

    if (!mapp(_score_dialog_preencoded_template)) {
        dialog_template = XK_PROTOCOL_CORE->xk_build_dialog_preencoded_template(
            this_object(), "角色状态", "",
            query_score_dialog_extra("ready_cached"));
        if (!mapp(dialog_template) || !mapp(dialog_template["stable_payload"])
            || !mapp(dialog_template["dynamic_defaults"]))
            return 0;
        _score_dialog_preencoded_template = ([
            "stable_payload": copy(dialog_template["stable_payload"]),
            "dynamic_defaults": copy(dialog_template["dynamic_defaults"]),
        ]);
    }
    return ([
        "stable_payload": copy(_score_dialog_preencoded_template["stable_payload"]),
        "dynamic_defaults": copy(_score_dialog_preencoded_template["dynamic_defaults"]),
    ]);
}
