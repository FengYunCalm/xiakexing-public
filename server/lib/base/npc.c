/**
 * @file    npc.c
 * @brief   NPC 基类



 *
 * @par Description:
 * 基础对象层子模块，供上层对象组合能力。
 */

#pragma strict_types

#include <system/daemon.h>
#include <system/services.h>
#include <framework/heartbeat_module.h>

inherit "/lib/base/living";

nosave string _npc_id;
nosave int _respawn_time;
nosave int _wander_interval;
nosave mapping _dialogue;
nosave string _npc_archetype;
nosave string _behavior_profile;
nosave string _dialogue_profile;
nosave int _ai_enabled;

#undef is_npc
#undef is_character

string render_dialogue_line(string bucket, object player, mapping context);
string query_dialogue_profile();
string query_quest_provider_id();
string query_primary_id();
int query_quest_supported(object player);
int can_teach_skills(object player);
string query_talk_entry_label();
mapping *query_resident_root_actions();
varargs int present_topic_reply(string topic, mapping replies, string source,
    string default_text);
varargs int present_interaction(object target, string title, string content,
    string dialog_id, mapping dialog_extra);
int present_provider_panel(object target, string title, string dialog_id);

private int target_supports_xk_protocol(object target)
{
    string version;

    if (!objectp(target) || !interactive(target))
        return 0;

    if (target->query_temp("client/plain_text_only"))
        return 0;

    version = target->query_temp("xk_protocol_version");
    if (stringp(version) && version != "")
        return 1;

    return 0;
}

private string resolve_interaction_target_id()
{
    mixed ids;
    string target_id;

    target_id = call_other("/lib/std/utils/util_player", "get_object_display_id",
        this_object(), "");
    if (stringp(target_id) && target_id != "")
        return target_id;

    target_id = query_quest_provider_id();
    if (target_id != "")
        return target_id;

    target_id = get_object_id(this_object());
    if (stringp(target_id) && target_id != "")
        return target_id;

    return "";
}

private string infer_dialog_bucket(string dialog_id)
{
    if (!stringp(dialog_id) || dialog_id == "")
        return "";
    if (strsrch(dialog_id, "_quests") != -1)
        return "quest";
    if (strsrch(dialog_id, "_progress") != -1)
        return "progress";
    if (strsrch(dialog_id, "_talk") != -1)
        return "talk";
    if (strsrch(dialog_id, "_help") != -1)
        return "help";
    if (strsrch(dialog_id, "_intro") != -1)
        return "intro";
    if (strsrch(dialog_id, "_goods") != -1)
        return "goods";

    return "";
}

private mapping *append_dialog_action(mapping *actions, string label, string cmd,
    string kind, string surface, string role, string target_group)
{
    mapping action;

    if (!arrayp(actions))
        actions = ({ });
    if (!stringp(label) || label == "" || !stringp(cmd) || cmd == "")
        return actions;

    foreach (action in actions) {
        if (mapp(action) && action["cmd"] == cmd)
            return actions;
    }

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
    return actions + ({ action });
}

private mapping *append_client_ask_topic_actions(mapping *actions, string target_id,
    object player)
{
    mixed *topics;
    mapping topic_def;
    string label;
    string topic;

    actions = arrayp(actions) ? actions : ({ });
    if (!stringp(target_id) || target_id == "" || !objectp(player))
        return actions;
    if (!function_exists("query_client_ask_topics", this_object()))
        return actions;

    topics = this_object()->query_client_ask_topics(player);
    if (!arrayp(topics) || sizeof(topics) == 0)
        return actions;

    foreach (topic_def in topics) {
        if (!mapp(topic_def))
            continue;
        topic = topic_def["topic"] || "";
        label = topic_def["label"] || "";
        if (topic == "")
            continue;
        if (label == "")
            label = topic;   /* 话题具名化，默认即话题名 */
        actions = append_dialog_action(actions, label,
            "ask " + target_id + " about " + topic, "ask", "conversation",
            "secondary", "conversation");
    }
    return actions;
}

private mapping build_dialog_action_group(string id, string title, mapping *actions)
{
    if (!arrayp(actions) || sizeof(actions) == 0)
        return 0;
    return ([
        "id": id,
        "title": title,
        "surface": id,
        "actions": actions,
    ]);
}

private mapping *merge_dialog_action_groups(mapping *existing_groups, mapping *incoming_groups)
{
    if (!arrayp(existing_groups) || sizeof(existing_groups) == 0)
        return arrayp(incoming_groups) ? incoming_groups : ({ });
    if (!arrayp(incoming_groups) || sizeof(incoming_groups) == 0)
        return existing_groups;
    return existing_groups + incoming_groups;
}

string query_quest_provider_id()
{
    /* 提供者身份单一真源：内容层统一写 query("provider_id")（create 与
     * apply_content_entity），此处只读不回写，避免双写源拷贝漂移导致
     * 消费方身份判定漂移。 */
    return stringp(query("provider_id")) ? query("provider_id") : "";
}

string query_primary_id()
{
    mixed ids;

    /* 主显示 id：query("id")[0]（各 NPC 模板均带 id/ids，运行期恒有值）。
     * 原 18 份 per-file private 拷贝（fallback 死代码）统一收敛到 base；
     * fallback 用 provider_id 兑底，语义不漂移。 */
    ids = function_exists("query_id", this_object())
        ? query_id() : get_object_id(this_object());
    if (arrayp(ids) && sizeof(ids) > 0 && stringp(ids[0]) && ids[0] != "")
        return ids[0];
    if (stringp(ids) && ids != "")
        return ids;
    return query_quest_provider_id();
}

/* 无话题业务 NPC 的交谈入口具名化 hook。默认空串（不下发任何
 * talk 入口，避免复述类死按钮）；业务 NPC 覆写返回功能名，root 组据此下发
 * 具名按钮（cmd 仍走 talk 复用 do_talk 业务面板）。多动作集场景请改用
 * query_resident_root_actions() 全集通道（本 hook 是其单按钮语法糖）。 */
string query_talk_entry_label()
{
    return "";
}

/* 设施常驻动作 hook（任意动作集全集通道）。默认空数组（不下发）；
 * 设施类子类覆写返回直操作动作集（如挂机点的打坐/练功/结束挂机），由 root 组
 * 并入常驻动作区，不经交谈弹出。label/cmd 必填，kind/surface/role/target_group
 * 缺省兜底 root/primary；单按钮弹 do_talk 面板的场景用 query_talk_entry_label()
 * （本 hook 的语法糖形态）。 */
mapping *query_resident_root_actions()
{
    return ({ });
}

int query_quest_supported(object player)
{
    string provider_id;
    mapping definitions;
    string quest_id;
    mapping definition;

    if (!objectp(player))
        return 0;

    /* 数据驱动能力谓词：该 NPC 是否对当前玩家有可见任务。
     * 不能仅凭 provider_id != ""——大量商贩/环境 NPC 带 provider_id 仅作商店/对话
     * bucket 身份用，并非真实任务提供者；仅凭身份会给他们多出「任务」按钮（点开是
     * 空面板）。较原 do_quest 实现者限定更窄：不仅要求实现者身份，还要求当下有
     * 可见任务才显入口（与房间面板语义一致）；按钮恒显（客户端合成），
     * 此谓词继续供房间面板等其它消费方判定 provider 身份；点击后的响应分流由
     * do_quest 经 query_provider_click_hint 承担。短路：命中第一条该
     * provider 可见任务即返回，避免全量收集（此谓词在对话框/房间投影热路径被调用）。 */
    provider_id = query_quest_provider_id();
    if (provider_id == "")
        return 0;
    definitions = QUEST_D->query_visible_quest_definitions(player);
    foreach (quest_id, definition in definitions) {
        if (mapp(definition) && definition["provider"] == provider_id)
            return 1;
    }
    return 0;
}

int do_quest(string arg)
{
    string hint;

    /* 数据驱动默认任务入口：打开该 NPC 的任务面板。原 10 份 per-file 逐字同构
     * 拷贝收敛到 base；present_provider_panel 内部对空 provider 返回 0，行为等价。
     * 分流决策与文案归 quest 域单门面 query_provider_click_hint
     * （空串=开面板；非空=进行中引导/无任务提示），base 层哑转发不持有业务分支。 */
    hint = QUEST_D->query_provider_click_hint(this_player(), query_quest_provider_id());
    if (hint != "") {
        /* 无任务/进行中这类轻量点击反馈走飘窗通道
         * （display_mode=float 的 MSGE），不占主文本区；telnet 纯文本
         * 客户端保持 msg_send 回退。XK 判定复用文件内属主
         * target_supports_xk_protocol（含 interactive 检查）；
         * npc.c 不继承 lib/framework/command（msg_event 运行时不可见，
         * debug.log 已实证），故经 MESSAGE_D 直发。 */
        if (target_supports_xk_protocol(this_player())) {
            MESSAGE_D->emit_message_event(this_player(), hint, ([
                "channel": "system",
                "intent": "notify",
                "priority": "normal",
                "reliability": "important",
                "display_mode": "float",
                "ttl_ms": 2000,
                "collapse_key": "float/quest_hint/" + query_primary_id(),
                "response_context": CLIENT_SYNC_D->capture_command_response_context(
                    this_player()),
                "payload": ([
                    "kind": "float_message",
                    "text": hint,
                    "level": "info",
                    "ok": 1,
                ]),
            ]));
            /* 客户端 ShellMessagePacketCoordinator 按此 collapse_key 前缀
             * （QUEST_HINT_COLLAPSE_PREFIX）结算挂起的交互请求，改名需双侧同步。 */
        } else {
            msg_send(this_player(), hint);
        }
        return 1;
    }
    return present_provider_panel(this_player(), query_name(),
        query_primary_id() + "_quests");
}

private mapping *build_interaction_action_groups(string target_id,
    mapping dialog_extra, object player)
{
    mapping *groups;
    mapping *root_actions;
    mapping *conversation_actions;
    mapping *trade_actions;
    mapping group;
    string provider_id;
    string source;
    string bucket;
    string action_target;
    string npc_type;
    string talk_entry_label;

    groups = ({ });
    root_actions = ({ });
    conversation_actions = ({ });
    trade_actions = ({ });
    if (!stringp(target_id) || target_id == "" || !objectp(player))
        return groups;

    source = mapp(dialog_extra) && stringp(dialog_extra["source"]) ? dialog_extra["source"] : "";
    bucket = mapp(dialog_extra) && stringp(dialog_extra["dialog_bucket"]) ? dialog_extra["dialog_bucket"] : "";
    provider_id = mapp(dialog_extra) && stringp(dialog_extra["provider_id"]) ? dialog_extra["provider_id"] : "";
    action_target = provider_id != "" ? provider_id : target_id;
    npc_type = query("npc_type") || "";

    if (strsrch(source, "npc_") == 0)
        root_actions = append_dialog_action(root_actions, "查看", "look " + target_id, "look", "root", "secondary", "root");
    if (npc_type == "banker" || query("banker"))
        root_actions = append_dialog_action(root_actions, "银行", "bank", "bank", "root", "primary", "root");
    if (npc_type == "healer" || query("healer"))
        root_actions = append_dialog_action(root_actions, "疗伤", "heal", "heal", "root", "primary", "root");
    if (can_teach_skills(player))
        root_actions = append_dialog_action(root_actions, "技能", "learn list " + action_target, "learn", "root", "secondary", "learn");
    if (source == "npc_interaction" && (npc_type == "vendor" || (stringp(query("shop_id")) && query("shop_id") != "")))
        root_actions = append_dialog_action(root_actions, "货单", "list", "trade", "root", "primary", "trade");
    /* 任务按钮门控（query_quest_supported）：当前玩家在提供者身份下存在可见任务才显。
     * 这是动态 per-player 查询（非恒显），与 client_projection_d 房间面板语义一致
     * （provider 有可见任务才显）；提供者无任务（如商贩）不显，避免空面板死按钮。 */
    if (query_quest_supported(player))
        root_actions = append_dialog_action(root_actions, "任务",
            "quest " + query_quest_provider_id(), "quest", "root", "secondary", "quest");
    /* conversation 组两侧（本文件与 client_projection_d）统一由话题非空驱动：
     * append_client_ask_topic_actions 内部自检 query_client_ask_topics 存在且非空。
     * 话题具名化后不再有伞状“交谈”按钮——话题按钮即一级入口。 */
    conversation_actions = append_client_ask_topic_actions(conversation_actions,
        target_id, player);
    /* 无话题业务 NPC 的具名功能入口。默认 query_talk_entry_label()
     * 返回空串不下发；业务 NPC 覆写返回功能名（如“领取武器”），cmd 复用 talk
     * 命令弹出 do_talk 业务面板，target_group 归 root 不再伪装话题。 */
    talk_entry_label = query_talk_entry_label();
    if (talk_entry_label != "" && (bucket == "talk" || source == "npc_provider_interaction"
        || source == "npc_profile_interaction" || source == "npc_provider_panel"))
        root_actions = append_dialog_action(root_actions, talk_entry_label, "talk " + action_target, "talk", "root", "primary", "root");
    if (source == "npc_profile_interaction" || source == "npc_provider_interaction" || source == "npc_interaction")
        root_actions = append_dialog_action(root_actions, "查看", "look " + target_id, "look", "root", "secondary", "root");
    if ((function_exists("do_fight", this_object()) || !query("no_fight")) && source != "npc_provider_panel")
        root_actions = append_dialog_action(root_actions, "切磋", "fight " + target_id, "fight", "root", "secondary", "root");
    /* 设施常驻动作并入 root 组（per-player 动态，如挂机状态切换）。
     * 归一化与投影侧同款（缺省键兜底），保证同一覆写经两路径行为一致；
     * 与切磋同款 provider 面板守卫。 */
    if (source != "npc_provider_panel")
        foreach (group in query_resident_root_actions())
            if (mapp(group) && stringp(group["label"]) && stringp(group["cmd"]))
                root_actions += ({ ([
                    "label": group["label"],
                    "cmd": group["cmd"],
                    "kind": stringp(group["kind"]) ? group["kind"] : "root",
                    "surface": stringp(group["surface"]) ? group["surface"] : "root",
                    "role": stringp(group["role"]) ? group["role"] : "primary",
                    "target_group": stringp(group["target_group"]) ? group["target_group"] : "root",
                ]) });
    /* 任务指引动作并入 root 组（进度感知：进行到该步且目标为本实体才显）。
     * 判定与产出归 QUEST_D->query_provider_step_actions 单一权威；match_ids 用本实体
     * 可解析 id 全集 + provider，覆盖步骤 target 写 template/provider id 的场景。
     * id 数组真键是 set("id",...)（"ids" 为死键，全仓零写入）。
     * provider 面板也并入指引动作——当前步指向该 NPC 时点「任务」
     * 开面板即见步骤按钮（如道谢），否则 TALK 收尾步无可达入口卡关。 */
    {
        mixed *guidance_actions;
        string *match_ids;
        match_ids = function_exists("query_id", this_object())
            ? query_id() : ({ get_object_id(this_object()) });
        guidance_actions = QUEST_D->query_provider_step_actions(player,
            (arrayp(match_ids) ? match_ids : ({ })) + ({ target_id, action_target }));
        foreach (group in guidance_actions)
            if (mapp(group) && stringp(group["label"]) && stringp(group["cmd"]))
                root_actions += ({ group });
    }


    if (source == "npc_interaction" && (npc_type == "vendor" || (stringp(query("shop_id")) && query("shop_id") != "")))
        trade_actions = append_dialog_action(trade_actions, "货单", "list", "trade", "trade", "primary", "trade");

    foreach (group in ({
        build_dialog_action_group("root", "主要操作", root_actions),
        build_dialog_action_group("conversation", "话题", conversation_actions),
        build_dialog_action_group("trade", "交易", trade_actions),
    })) {
        if (mapp(group))
            groups += ({ group });
    }
    foreach (group in groups)
        group["actions"] = CONTENT_MODE_POLICY_D->project_actions(
            player, group["actions"]);
    return groups;
}

/* 表驱动话题回复：replies 为 topic→回复文案 mapping。命中（或
 * 提供 default_text 兜底）时以 msg_send 文本直达主区（自署名语料逐字发送）；
 * 双双未命中返 0 由调用方落通用兜底。 */
varargs int present_topic_reply(string topic, mapping replies, string source,
    string default_text)
{
    object me;
    string text;

    me = this_player();
    if (!objectp(me) || !stringp(topic) || topic == "" || !mapp(replies))
        return 0;
    text = replies[topic];
    if ((!stringp(text) || text == "") && stringp(default_text) && default_text != "")
        text = default_text;   /* miss 兜底文案，保持旧手写块"显式不知道"语义 */
    if (!stringp(text) || text == "")
        return 0;
    /* 纯文本话题回复回归主文本区（msg_send channel=main）。
     * 自署名守卫（单一属主）：语料已以本名开头则逐字发送，否则补名前缀——
     * 消除「某某：某某说：…」双署名（33 ambient 自署名语料与裸引号体并存）。 */
    if (query_name() != "" && strsrch(text, query_name()) == 0)
        msg_send(me, text);
    else
        msg_send(me, query_name() + "：" + text + "\n");
    return 1;
}

int is_npc()
{
    return 1;
}

int is_character()
{
    return 1;
}

varargs int can_accept_give_item(object player, object item, int count)
{
    string *matching_quests;

    if (query("accept_any_gift"))
        return 1;
    if (!objectp(player) || !objectp(item))
        return 0;

    matching_quests = QUEST_D->query_matching_give_quests(player,
        this_object(), item, count > 0 ? count : 1);
    return arrayp(matching_quests) && sizeof(matching_quests) > 0;
}

int can_accept_give_money(object player, int amount)
{
    if (query("accept_any_gift") || query("accept_money_gift"))
        return 1;
    return 0;
}

mixed *query_teachable_skills(object player)
{
    if (!objectp(player))
        return ({ });
    return SKILL_SOURCE_D->query_teacher_skill_offerings(player, this_object());
}

int can_teach_skills(object player)
{
    return sizeof(query_teachable_skills(player)) > 0;
}

int query_teach_skill_limit(string skill_id)
{
    mapping limits;
    int limit;

    if (!stringp(skill_id) || skill_id == "")
        return query("teach_skill_max") || 20;
    limits = query("teach_skill_limits");
    if (mapp(limits) && intp(limits[skill_id]) && limits[skill_id] > 0)
        return limits[skill_id];
    limit = query("teach_skill_max") || 0;
    return limit > 0 ? limit : 20;
}

int present_skill_learning_panel(object player)
{
    mapping *actions;
    mapping extra;
    mapping offering;
    mixed *offerings;
    string target_id;
    string text;

    if (!objectp(player))
        return 0;
    offerings = query_teachable_skills(player);
    if (!arrayp(offerings) || sizeof(offerings) == 0) {
        return present_interaction(player, query_name() || query("name") || "对方",
            "此人当前没有可以传授给你的技能。\n", "skill_learning",
            ([ "source": "npc_learning", "dialog_bucket": "skills" ]));
    }

    target_id = resolve_interaction_target_id();
    actions = ({ });
    text = "技能：\n";
    foreach (offering in offerings) {
        if (!mapp(offering))
            continue;
        text += sprintf("- %s：%d/%d",
            offering["display_name"] || offering["skill_id"],
            offering["level"] || 0,
            offering["max_skill"] || 0);
        if (stringp(offering["display_status"]) && offering["display_status"] != "")
            text += "，" + offering["display_status"];
        text += "\n";
        actions = append_dialog_action(actions,
            offering["display_name"] || offering["skill_id"],
            "learn " + offering["skill_id"] + " from " + target_id,
            "learn_skill", "learn", "primary", "learn");
    }

    extra = ([
        "source": "npc_learning",
        "dialog_bucket": "skills",
        "action_groups": ({ build_dialog_action_group("learn", "技能", actions) }),
    ]);
    return present_interaction(player, query_name() || query("name") || "对方",
        text, "skill_learning", extra);
}

varargs int reject_give_item(object player, object item, int count)
{
    string text;
    string npc_name;

    if (!objectp(player))
        return 0;

    npc_name = query_name() || query("name") || "对方";
    text = query("gift_refusal_text");
    if (!stringp(text) || text == "")
        text = npc_name + "摆了摆手：这东西你自己留着吧，我用不上。\n";
    return present_interaction(player, npc_name, text, "gift_refusal");
}

int reject_give_money(object player, int amount)
{
    string text;
    string npc_name;

    if (!objectp(player))
        return 0;

    npc_name = query_name() || query("name") || "对方";
    text = query("money_gift_refusal_text");
    if (!stringp(text) || text == "")
        text = npc_name + "摆了摆手：这些钱你自己留着吧。\n";
    return present_interaction(player, npc_name, text, "gift_refusal");
}

void set_npc_id(string id)
{
    _npc_id = id;
}

string query_npc_id()
{
    return _npc_id || "";
}

void set_respawn_time(int seconds)
{
    _respawn_time = seconds;
}

int query_respawn_time()
{
    return _respawn_time;
}

void set_wander_interval(int seconds)
{
    _wander_interval = seconds;
}

int query_wander_interval()
{
    return _wander_interval;
}

void set_dialogue(mapping data)
{
    _dialogue = data;
}

void set_npc_archetype(string archetype)
{
    _npc_archetype = stringp(archetype) ? archetype : "";
}

string query_npc_archetype()
{
    return _npc_archetype || "";
}

void set_behavior_profile(string profile_id)
{
    _behavior_profile = stringp(profile_id) ? profile_id : "";
}

string query_behavior_profile()
{
    return _behavior_profile || "";
}

void set_dialogue_profile(string profile_id)
{
    _dialogue_profile = stringp(profile_id) ? profile_id : "";
}

string query_dialogue_profile()
{
    return _dialogue_profile || "";
}

void set_ai_enabled(int enabled)
{
    _ai_enabled = enabled ? 1 : 0;

    if (_ai_enabled)
        register_batched_heartbeat("buffs", "update_buffs", 1);
    else
        unregister_batched_heartbeat("buffs");

    set_heart_beat(_ai_enabled);
}

varargs int send_xk_dialog(object target, string title, string content,
    mapping extra, mapping response_context)
{
    mapping tx;

    if (!objectp(target))
        return 0;

    if (!target_supports_xk_protocol(target)) {
        if (stringp(content) && content != "")
            msg_send(target, content);
        return 1;
    }

    tx = CLIENT_SYNC_D->tx_add_dialog(0, title, content,
        mapp(extra) ? extra : ([]));
    tx = CLIENT_SYNC_D->tx_with_response_context(tx, response_context);
    CLIENT_SYNC_D->submit_transaction(target, tx);
    return 1;
}

varargs int present_interaction(object target, string title, string content,
    string dialog_id, mapping dialog_extra)
{
    mapping extra;
    mapping response_context;
    string plain_text;
    string target_id;

    if (!objectp(target) || !stringp(content) || content == "")
        return 0;

    response_context = target == this_player()
        ? CLIENT_SYNC_D->capture_command_response_context(target) : 0;
    extra = DIALOG_PROJECTION_D->build_dialog_extra("npc_interaction",
        dialog_id, dialog_extra);
    plain_text = mapp(extra) && stringp(extra["plain_text"]) && extra["plain_text"] != ""
        ? extra["plain_text"]
        : content;
    map_delete(extra, "plain_text");
    if (!target_supports_xk_protocol(target)) {
        msg_send(target, plain_text);
        return 1;
    }
    if ((!stringp(extra["dialog_bucket"]) || extra["dialog_bucket"] == "")
        && stringp(dialog_id) && dialog_id != "")
        extra["dialog_bucket"] = infer_dialog_bucket(dialog_id);
    target_id = resolve_interaction_target_id();
    if (stringp(target_id) && target_id != "") {
        if (!stringp(extra["source"]) || extra["source"] == ""
            || extra["source"] == "npc_interaction")
            extra["source"] = "npc_" + target_id;
        if (!stringp(extra["speaker_id"]) || extra["speaker_id"] == "")
            extra["speaker_id"] = target_id;
        extra["action_groups"] = merge_dialog_action_groups(extra["action_groups"],
            build_interaction_action_groups(target_id, extra, target));
    }
    return send_xk_dialog(target, title, content, extra, response_context);
}

int present_provider_interaction(object target, string title, string fallback_text, string dialog_id)
{
    string provider_id;

    provider_id = query_quest_provider_id();
    if (!objectp(target) || provider_id == "")
        return 0;
    return NPC_VIEW_D->present_provider_interaction(target, this_object(), provider_id,
        title, fallback_text, dialog_id);
}

int present_provider_panel(object target, string title, string dialog_id)
{
    string provider_id;

    provider_id = query_quest_provider_id();
    if (!objectp(target) || provider_id == "")
        return 0;
    return NPC_VIEW_D->present_provider_panel(target, this_object(), provider_id,
        title, dialog_id);
}

int present_dialogue_bucket(object target, string title, string bucket,
    string fallback_text, string dialog_id, mapping context)
{
    string text;

    if (!objectp(target) || !stringp(bucket) || bucket == "")
        return 0;

    text = render_dialogue_line(bucket, target, mapp(context) ? context : ([]));
    if (!stringp(text) || text == "")
        text = fallback_text;
    if (!stringp(text) || text == "")
        return 0;

    return present_interaction(target, title, text, dialog_id);
}

int present_quest_provider_interaction(object target, string title, string bucket,
    string fallback_text, string dialog_id, mapping context)
{
    string text;
    string provider_id;

    provider_id = query_quest_provider_id();
    if (!objectp(target) || provider_id == "")
        return 0;
    if (!stringp(bucket) || bucket == "")
        return 0;

    text = render_dialogue_line(bucket, target, mapp(context) ? context : ([]));
    if (!stringp(text) || text == "")
        text = fallback_text;
    if (!stringp(text) || text == "")
        return 0;

    return present_provider_interaction(target, title, text, dialog_id);
}

private int query_vendor_display_price(string item_id)
{
    string shop_id;
    int base_price;

    if (!stringp(item_id) || item_id == "")
        return 0;

    base_price = ITEM_D->get_item_value(item_id);
    if (base_price <= 0)
        return 0;

    shop_id = query("shop_id");
    return ECONOMY_SETTLEMENT_D->query_shop_buy_price(shop_id, item_id, base_price);
}

void restore_combat_vitals()
{
    /*
     * NPC 当前生命/内力的主路径已经统一到 `combat/...`。
     * 这里恢复主路径，旧 `qi/jing` 镜像是否同步由具体 NPC 或兼容层决定。
     */
    if (query("combat/hp") < query("combat/max_hp"))
        set("combat/hp", query("combat/max_hp"));
    if (query("combat/mp") < query("combat/max_mp"))
        set("combat/mp", query("combat/max_mp"));
}

mapping query_dialogue()
{
    mapping result;
    mapping profile;
    mapping keyword_responses;

    result = ([]);
    if (stringp(_dialogue_profile) && _dialogue_profile != "") {
        profile = DIALOG_D->query_dialogue_profile(_dialogue_profile);
        keyword_responses = mapp(profile) && mapp(profile["keyword_responses"])
            ? profile["keyword_responses"]
            : 0;
        if (mapp(keyword_responses))
            result += copy(keyword_responses);
    }
    if (mapp(_dialogue))
        result += copy(_dialogue);
    return result;
}

mapping query_behavior_config()
{
    mapping behavior;
    mapping overrides;

    if (!stringp(_behavior_profile) || _behavior_profile == "")
        behavior = ([]);
    else
        behavior = NPC_BEHAVIOR_D->query_behavior_profile(_behavior_profile);

    if (!mapp(behavior))
        behavior = ([]);

    overrides = query("behavior_overrides");
    if (mapp(overrides))
        behavior += overrides;
    if (stringp(_npc_archetype) && _npc_archetype != "")
        behavior["archetype"] = _npc_archetype;
    if (stringp(_behavior_profile) && _behavior_profile != "")
        behavior["profile_id"] = _behavior_profile;

    return behavior;
}

string render_dialogue_line(string bucket, object player, mapping context)
{
    if (!stringp(_dialogue_profile) || _dialogue_profile == "")
        return 0;
    return DIALOG_D->render_dialogue_text(_dialogue_profile, bucket, player, context);
}

string respond_dialogue(object player, string msg)
{
    mapping dialogue;
    string *all_keys;
    int index;

    dialogue = query_dialogue();
    if (!mapp(dialogue) || !stringp(msg) || msg == "")
        return 0;

    all_keys = keys(dialogue);
    for (index = 0; index < sizeof(all_keys); index++) {
        if (strsrch(msg, all_keys[index]) != -1)
            return dialogue[all_keys[index]];
    }

    return 0;
}

void set_weapon(string weapon_path)
{
    set("weapon", weapon_path);
}

private string normalize_vendor_item_id(string entry)
{
    string *parts;

    if (!stringp(entry) || entry == "")
        return "";

    if (strsrch(entry, "/") == -1)
        return entry;

    parts = explode(entry, "/");
    if (!sizeof(parts))
        return entry;

    entry = parts[<1];
    if (strlen(entry) > 2 && entry[<2..] == ".c")
        entry = entry[0..<3];

    return entry;
}

mapping query_all_goods()
{
    mixed goods;
    mapping result;
    string entry;
    string item_id;
    string item_name;
    int item_price;

    /*
     * 这里把 NPC 的 `vendor_goods` 列表收敛成统一商品映射，
     * 目的是让 buy/sell/list/item_shop 共享同一份商人契约，
     * 不再让每个商人 NPC 自己实现一套本地交易真逻辑。
     */
    goods = query("vendor_goods");
    if (!arrayp(goods) || sizeof(goods) == 0)
        return ([]);

    result = ([]);
    foreach (entry in goods) {
        item_id = normalize_vendor_item_id(entry);
        if (!stringp(item_id) || item_id == "")
            continue;

        item_name = ITEM_D->get_item_name(item_id);
        item_price = query_vendor_display_price(item_id);
        if (!stringp(item_name) || item_name == "未知物品" || item_price <= 0)
            continue;

        result[item_id] = ([
            "name": item_name,
            "price": item_price,
            "stock": -1
        ]);
    }

    return result;
}

string render_vendor_goods_text(string intro, string outro)
{
    mixed goods;
    string entry;
    string item_id;
    string item_name;
    int item_price;
    string text;

    goods = query("vendor_goods");
    if (!arrayp(goods) || sizeof(goods) == 0)
        return stringp(intro) ? intro : "";

    text = stringp(intro) && intro != "" ? intro + "\n" : "";
    text += "\"本店当前货单如下：\n\n";

    foreach (entry in goods) {
        item_id = normalize_vendor_item_id(entry);
        if (!stringp(item_id) || item_id == "")
            continue;

        item_name = ITEM_D->get_item_name(item_id);
        item_price = query_vendor_display_price(item_id);
        if (!stringp(item_name) || item_name == "未知物品" || item_price <= 0)
            continue;

        text += "  " + item_name + " - " + sprintf("%d", item_price) + "文\n";
    }

    if (stringp(outro) && outro != "")
        text += "\n" + outro;

    text += "\"\n";
    return text;
}

void set_armor(string armor_path)
{
    set("armor", armor_path);
}

void equip_preset()
{
}

void create()
{
    ::create();
    enable_commands();
    _ai_enabled = 0;
    set_heartbeat_batch_name(HEARTBEAT_BATCH_NPC);
    set_heartbeat_profile(HEARTBEAT_PROFILE_NPC, ([
        "batch": HEARTBEAT_BATCH_NPC,
        "interval": 1,
    ]));
    set_heart_beat(0);
}

void reset()
{
    restore_combat_vitals();
}

void cleanup_dead_npc()
{
    int hp;

    if (interactive(this_object()))
        return;

    hp = query("combat/hp");
    if (query_temp("is_dying") || (intp(hp) && hp <= 0))
        destruct(this_object());
}

void on_die(object killer)
{
    object env;
    int respawn_at;

    if (interactive(this_object()))
        return;

    env = environment(this_object());
    respawn_at = query_respawn_time();
    if (objectp(env) && intp(respawn_at) && respawn_at > 0)
        call_other("/lib/std/utils/util_view", "set_room_respawn_deadline", env, base_name(this_object()), time() + respawn_at);

    clear_batched_heartbeats();
    set_heart_beat(0);
    call_out("cleanup_dead_npc", 0);
}

void heart_beat()
{
}
