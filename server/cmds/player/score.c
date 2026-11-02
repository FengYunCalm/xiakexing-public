/**
 * @file    score.c
 * @brief   属性查看命令 - 显示玩家属性



 *
 * @par Description:
 * 显示玩家基础属性和战斗属性，发送XK协议状态。
 *
 * @par Usage:
 *   score - 显示角色状态
 *
 */

#pragma strict_types

#include <system/ansi.h>
#include <system/services.h>
#include <game/command.h>
#include <system/xk_protocol.h>

#include <framework/message_facade.h>

inherit "/lib/framework/network";
inherit "/lib/std/utils/util_status";
inherit "/lib/std/utils/util_player";
inherit "/lib/std/utils/util_owner_projection";

#define SCORE_RENDER_CACHE_TTL 5
#define XK_PROTOCOL_CORE "/kernel/framework/xk_protocol_core"

int help(object me);
int main(object me, string arg);
string show_status(object me);
private string show_status_dialog(object me, mapping analysis);
private string query_score_worker_state(mapping analysis);
private int is_ready_worker_state(string worker_state);
private mapping query_cached_score_dialog_entry_with_signature(object me,
    string signature);
private void remember_score_dialog_render_with_signature(object me,
    string content, string worker_state, string signature);
void xk_prime_score_dialog_cache(object me);
mapping xk_score_dialog_extra(string worker_state);

mapping xk_score_dialog_extra(string worker_state)
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
        "read_side_worker": is_ready_worker_state(worker_state) ? 1 : 0,
        "read_side_worker_state": worker_state || "pending",
    ]);
}

private void emit_score_dialog(object me, string content, string worker_state,
    mapping response_context)
{
    mapping extra;

    if (!objectp(me) || !stringp(content) || content == "")
        return;

    extra = xk_score_dialog_extra(worker_state);
    xk_send_dialog(me, "角色状态", content, extra, response_context);
}

private string query_score_render_signature(object me)
{
    if (!objectp(me))
        return "invalid";
    return CLIENT_PROJECTION_D->query_score_projection_live_token(me);
}

private mapping query_cached_score_dialog_entry(object me)
{
    if (!objectp(me))
        return 0;

    return query_cached_score_dialog_entry_with_signature(me,
        query_score_render_signature(me));
}

private mapping query_cached_score_dialog_entry_with_signature(object me,
    string signature)
{
    mapping cache;

    if (!objectp(me) || !stringp(signature) || signature == "")
        return 0;

    cache = me->query_temp("xk/score_dialog_cache");
    if (!mapp(cache)
        || !stringp(cache["content"])
        || cache["content"] == ""
        || cache["signature"] != signature
        || !intp(cache["at"])
        || time() - cache["at"] > SCORE_RENDER_CACHE_TTL)
        return 0;

    return cache;
}

private string query_cached_score_render(object me)
{
    mapping cache;
    string signature;

    if (!objectp(me))
        return "";

    cache = me->query_temp("xk/score_render_cache");
    signature = CLIENT_PROJECTION_D->query_score_projection_live_token(me);
    if (!mapp(cache)
        || !stringp(cache["content"])
        || cache["content"] == ""
        || cache["signature"] != signature
        || !intp(cache["at"])
        || time() - cache["at"] > SCORE_RENDER_CACHE_TTL)
        return "";

    return cache["content"];
}

private string query_cached_score_dialog_render(object me)
{
    mapping cache;

    cache = query_cached_score_dialog_entry(me);
    return mapp(cache) && stringp(cache["content"]) ? cache["content"] : "";
}

private string query_cached_score_dialog_worker_state(object me)
{
    mapping cache;

    cache = query_cached_score_dialog_entry(me);
    if (!mapp(cache))
        return "";

    return stringp(cache["read_side_worker_state"])
        ? cache["read_side_worker_state"]
        : "";
}

private void remember_score_render(object me, string content)
{
    if (!objectp(me) || !stringp(content) || content == "")
        return;

    me->set_temp("xk/score_render_cache", ([
        "at": time(),
        "signature": query_score_render_signature(me),
        "content": content,
    ]));
}

private void remember_score_dialog_render(object me, string content,
    string worker_state)
{
    remember_score_dialog_render_with_signature(me, content, worker_state,
        query_score_render_signature(me));
}

private void remember_score_dialog_render_with_signature(object me,
    string content, string worker_state, string signature)
{
    if (!objectp(me) || !stringp(content) || content == "")
        return;
    if (!stringp(signature) || signature == "")
        return;

    me->set_temp("xk/score_dialog_cache", ([
        "at": time(),
        "signature": signature,
        "read_side_worker_state": stringp(worker_state) && worker_state != ""
            ? worker_state
            : "pending",
        "content": content,
    ]));
}

private int is_ready_worker_state(string worker_state)
{
    return stringp(worker_state)
        && (worker_state == "ready"
            || worker_state == "ready_cached");
}

private string promote_cached_score_worker_state(string worker_state)
{
    if (!stringp(worker_state) || worker_state == "")
        return "pending_cached";
    if (worker_state == "ready" || worker_state == "ready_cached")
        return "ready_cached";
    if (worker_state == "pending" || worker_state == "pending_cached")
        return "pending_cached";
    return worker_state;
}


/**
 * 格式化头部信息
 * @param me 玩家对象
 * @return 头部信息字符串
 */
private string format_header(object me)
{
    string name = get_object_display_name(me, "无名侠客");
    string title = me->query("title") || "";
    string result = "";

    result += HIC "╔══════════════════════════════════════╗" NOR "\n";

    if (title != "")
        result += sprintf(HIC "║" NOR HIY " %-36s " NOR HIC "║" NOR "\n", name + " · " + title);
    else
        result += sprintf(HIC "║" NOR HIY " %-36s " NOR HIC "║" NOR "\n", name);

    result += HIC "╠══════════════════════════════════════╣" NOR "\n";

    return result;
}

/**
 * 格式化气血显示
 * @param hp 当前气血
 * @param max_hp 最大气血
 * @return 气血显示字符串
 */
private string format_hp_bar(int hp, int max_hp)
{
    string color = (hp * 100 / max_hp < 30) ? HIR : HIG;

    return sprintf(HIC "║" NOR "  气血: %s%3d%s/%-3d %-20s " HIC "║" NOR "\n",
        color, hp, NOR, max_hp,
        "████████████"[max(0, 11 - hp * 12 / max_hp)..11]);
}

/**
 * 格式化内力显示
 * @param mp 当前内力
 * @param max_mp 最大内力
 * @return 内力显示字符串
 */
private string format_mp_bar(int mp, int max_mp)
{
    return sprintf(HIC "║" NOR "  内力: %s%3d%s/%-3d %-20s " HIC "║" NOR "\n",
        HIB, mp, NOR, max_mp,
        "████████████"[max(0, 11 - mp * 12 / max_mp)..11]);
}

/**
 * 格式化战斗状态
 * @param me 玩家对象
 * @return 战斗状态字符串
 */
private string format_combat_status(object me)
{
    string result = "";
    mapping combat;
    int hp, max_hp, mp, max_mp;

    combat = query_combat_resource_snapshot(me);
    hp = combat["hp"];
    max_hp = combat["max_hp"];
    mp = combat["mp"];
    max_mp = combat["max_mp"];

    result += HIC "║" NOR " " HIR "【战斗状态】" NOR "                          " HIC "║" NOR "\n";
    result += format_hp_bar(hp, max_hp);
    result += format_mp_bar(mp, max_mp);
    result += HIC "╠══════════════════════════════════════╣" NOR "\n";

    return result;
}

/**
 * 格式化基础属性
 * @param me 玩家对象
 * @return 基础属性字符串
 */
private string format_attributes(object me)
{
    int str = me->query("attributes/strength") || 10;
    int dex = me->query("attributes/dexterity") || 10;
    int con = me->query("attributes/constitution") || 10;
    int int_val = me->query("attributes/intelligence") || me->query("int") || 10;

    return sprintf(
        HIC "║" NOR " " HIW "【先天属性】" NOR "                          " HIC "║" NOR "\n" HIC "║" NOR "  臂力: %-3d    身法: %-3d              " HIC "║" NOR "\n" HIC "║" NOR "  根骨: %-3d    悟性: %-3d              " HIC "║" NOR "\n" HIC "╠══════════════════════════════════════╣" NOR "\n",
        str, dex, con, int_val
    );
}

/**
 * 格式化成长概览
 * @param me 玩家对象
 * @return 成长概览字符串
 */
private string format_progression(object me)
{
    mapping snapshot;
    int exp;
    int progression_rank;
    int skill_cap;

    snapshot = query_player_public_snapshot(me);
    exp = snapshot["exp"];
    progression_rank = snapshot["progression_rank"];
    skill_cap = snapshot["skill_cap"];

    return sprintf(
        HIC "║" NOR " " HIM "【成长概览】" NOR "                          " HIC "║" NOR "\n" HIC "║" NOR "  江湖阅历: %-10d              " HIC "║" NOR "\n" HIC "║" NOR "  成长档位: %-3d    技能上限: %-4d  " HIC "║" NOR "\n" HIC "╠══════════════════════════════════════╣" NOR "\n",
        exp, progression_rank, skill_cap
    );
}

private string format_actor_analysis(object me)
{
    mapping analysis;
    string async_state;
    int hp_pct;
    int resource_pct;
    int total_pct;

    analysis = PLAYER_ACTOR_ANALYSIS_D->query_player_actor_analysis(me);
    if (!mapp(analysis) || !analysis["ok"]) {
        async_state = mapp(analysis) && stringp(analysis["async_state"])
            ? analysis["async_state"]
            : "";
        return HIC "║" NOR " " HIB "【多核评估】" NOR "                          " HIC "║" NOR "\n"
            HIC "║" NOR "  后台状态评估: " + (async_state == "pending" ? "计算中" : "暂不可用")
            + "              " HIC "║" NOR "\n"
            HIC "╠══════════════════════════════════════╣" NOR "\n";
    }

    hp_pct = (analysis["hp_pct_bp"] || 0) / 100;
    resource_pct = (analysis["resource_score"] || 0) / 100;
    total_pct = (analysis["total_score"] || 0) / 100;

    return sprintf(
        HIC "║" NOR " " HIB "【多核评估】" NOR "                          " HIC "║" NOR "\n" HIC "║" NOR "  生存: %-3d%%    资源: %-3d%%            " HIC "║" NOR "\n" HIC "║" NOR "  综合: %-3d%%    状态: %-10s      " HIC "║" NOR "\n" HIC "╠══════════════════════════════════════╣" NOR "\n",
        hp_pct, resource_pct, total_pct, analysis["state"] || "unknown"
    );
}

/**
 * 格式化装备状态
 * @param me 玩家对象
 * @return 装备状态字符串
 */
private string format_equipment(object me)
{
    object weapon = me->query_temp("weapon");
    string weapon_name = get_object_display_name(weapon, "空手");

    return sprintf(
        HIC "║" NOR " " YEL "【装备状态】" NOR "                          " HIC "║" NOR "\n" HIC "║" NOR "  武器: %-30s " HIC "║" NOR "\n" HIC "╚══════════════════════════════════════╝" NOR "\n",
        weapon_name
    );
}

private string format_actor_analysis_dialog(mapping analysis)
{
    string async_state;
    int hp_pct;
    int resource_pct;
    int total_pct;

    if (!mapp(analysis) || !analysis["ok"]) {
        async_state = mapp(analysis) && stringp(analysis["async_state"])
            ? analysis["async_state"]
            : "";
        return "【多核评估】 后台状态评估: "
            + (async_state == "pending" ? "计算中" : "暂不可用");
    }

    hp_pct = (analysis["hp_pct_bp"] || 0) / 100;
    resource_pct = (analysis["resource_score"] || 0) / 100;
    total_pct = (analysis["total_score"] || 0) / 100;

    return sprintf("【多核评估】 生存 %d%%  资源 %d%%  综合 %d%%  状态 %s",
        hp_pct, resource_pct, total_pct, analysis["state"] || "unknown");
}

private string query_score_worker_state(mapping analysis)
{
    if (mapp(analysis) && stringp(analysis["async_state"])
        && analysis["async_state"] != "")
        return analysis["async_state"];
    return mapp(analysis) && analysis["ok"] ? "ready" : "pending";
}

private string format_equipment_dialog(object me)
{
    object weapon;

    weapon = me->query_temp("weapon");
    return "【装备状态】 武器 "
        + get_object_display_name(weapon, "空手");
}

private string format_combat_status_dialog(object me)
{
    mapping combat;

    combat = query_combat_resource_snapshot(me);
    return sprintf("【战斗状态】 气血 %d/%d  内力 %d/%d",
        combat["hp"] || 0,
        combat["max_hp"] || 0,
        combat["mp"] || 0,
        combat["max_mp"] || 0);
}

private string format_attributes_dialog(object me)
{
    return sprintf("【先天属性】 臂力 %d  身法 %d  根骨 %d  悟性 %d",
        me->query("attributes/strength") || 10,
        me->query("attributes/dexterity") || 10,
        me->query("attributes/constitution") || 10,
        me->query("attributes/intelligence") || me->query("int") || 10);
}

private string format_progression_dialog(object me)
{
    mapping snapshot;

    snapshot = query_player_public_snapshot(me);
    return sprintf("【成长概览】 阅历 %d  档位 %d  上限 %d",
        snapshot["exp"] || 0,
        snapshot["progression_rank"] || 0,
        snapshot["skill_cap"] || 0);
}

string show_status(object me)
{
    if (!me) return "";

    return format_header(me) +
           format_combat_status(me) +
           format_attributes(me) +
           format_progression(me) +
           format_actor_analysis(me) +
           format_equipment(me);
}

private string show_status_dialog(object me, mapping analysis)
{
    mapping snapshot;

    if (!objectp(me))
        return "";
    snapshot = CLIENT_PROJECTION_D->query_score_projection_snapshot(me);
    if (mapp(analysis))
        snapshot["actor_analysis"] = copy(analysis);
    return render_score_dialog_snapshot(snapshot);
}

void xk_prime_score_dialog_cache(object me)
{
    mapping analysis;
    string worker_state;

    if (!objectp(me) || !interactive(me) || !is_xk_client(me))
        return;
    if (mapp(query_cached_score_dialog_entry(me)))
        return;
    if (function_exists("schedule_owner_score_read_model_refresh", me))
        me->schedule_owner_score_read_model_refresh(1);

    analysis = PLAYER_ACTOR_ANALYSIS_D->query_player_actor_analysis(me);
    worker_state = query_score_worker_state(analysis);
    if (!is_ready_worker_state(worker_state))
        return;

    remember_score_dialog_render(me, show_status_dialog(me, analysis), worker_state);
}

int main(object me, string arg)
{
    mapping analysis;
    mapping dialog_cache;
    mapping dialog_template;
    mapping owner_snapshot;
    mapping projection_snapshot;
    mapping read_model;
    string content;
    string signature;
    string worker_state;
    int artifact_result;
    mapping response_context;

    if (!objectp(me))
        return 0;
    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    if (stringp(arg) && lower_case(arg) == "xk") {
        CLIENT_SYNC_D->request_status(me, response_context);
        return 1;
    }

    if (!is_xk_client(me)) {
        content = query_cached_score_render(me);
        if (content == "") {
            content = show_status(me);
            remember_score_render(me, content);
        }
        msg_send(me, content);
    } else {
        signature = CLIENT_PROJECTION_D->query_score_projection_live_token(me);
        if (function_exists("query_owner_read_model", me)) {
            read_model = me->query_owner_read_model(
                "score_dialog", signature, 0);
            if (mapp(read_model) && read_model["state"] == "ready"
                && mapp(read_model["view"])
                && stringp(read_model["view"]["content"])
                && read_model["view"]["content"] != "") {
                content = read_model["view"]["content"];
                worker_state = read_model["read_side_worker_state"] || "ready";
                remember_score_dialog_render_with_signature(me, content,
                    worker_state, signature);
                artifact_result = call_other(XK_PROTOCOL_CORE,
                    "xk_send_preencoded_artifact", me, XK_DLOG,
                    read_model["preencoded"], response_context);
                /* L-36：0=未填充可安全回退；1/-1（已填充待对账）都已投递，
                 * 回退重发会产生重复业务包。 */
                if (intp(artifact_result) && artifact_result != 0)
                    return 1;
                emit_score_dialog(me, content,
                    promote_cached_score_worker_state(worker_state),
                    response_context);
                return 1;
            }
        }
        projection_snapshot = CLIENT_PROJECTION_D->query_score_projection_snapshot(me);
        signature = CLIENT_PROJECTION_D->query_score_projection_live_token_from_snapshot(
            me, projection_snapshot);
        dialog_cache = query_cached_score_dialog_entry_with_signature(me,
            signature);
        if (mapp(dialog_cache)) {
            content = dialog_cache["content"];
            worker_state = promote_cached_score_worker_state(
                stringp(dialog_cache["read_side_worker_state"])
                    ? dialog_cache["read_side_worker_state"]
                    : "");
        } else {
            owner_snapshot = copy(projection_snapshot);
            owner_snapshot["payload_key"] = "xk/score-dialog-owner-projection/v1";
            owner_snapshot["projection_kind"] = "score_dialog";
            owner_snapshot["signature"] = signature;
            owner_snapshot["read_side_worker_state"] = "ready";
            dialog_template = CLIENT_PROJECTION_D->query_score_dialog_preencoded_template();
            if (mapp(dialog_template) && mapp(dialog_template["stable_payload"])
                && mapp(dialog_template["dynamic_defaults"])) {
                owner_snapshot["protocol_stable_payload"] =
                    copy(dialog_template["stable_payload"]);
                owner_snapshot["protocol_dynamic_defaults"] =
                    copy(dialog_template["dynamic_defaults"]);
            }
            if (mapp(dialog_template) && mapp(dialog_template["stable_payload"])
                && mapp(dialog_template["dynamic_defaults"])
                && function_exists("publish_read_model_artifact", me)) {
                me->publish_read_model_artifact("score_dialog", owner_snapshot,
                    signature, mapp(read_model)
                        && read_model["state"] == "stale" ? 1 : 0);
                if (function_exists("query_owner_read_model", me)) {
                    read_model = me->query_owner_read_model(
                        "score_dialog", signature, 0);
                    if (mapp(read_model) && read_model["state"] == "ready"
                        && mapp(read_model["view"])
                        && stringp(read_model["view"]["content"])
                        && read_model["view"]["content"] != "") {
                        content = read_model["view"]["content"];
                        worker_state = read_model["read_side_worker_state"]
                            || "ready";
                        remember_score_dialog_render_with_signature(me, content,
                            worker_state, signature);
                        artifact_result = call_other(XK_PROTOCOL_CORE,
                            "xk_send_preencoded_artifact", me, XK_DLOG,
                            read_model["preencoded"], response_context);
                        /* L-36：-1（已填充待对账）不得回退重发。 */
                        if (intp(artifact_result) && artifact_result != 0)
                            return 1;
                        emit_score_dialog(me, content,
                            promote_cached_score_worker_state(worker_state),
                            response_context);
                        return 1;
                    }
                }
            }
            analysis = projection_snapshot["actor_analysis"];
            worker_state = query_score_worker_state(analysis);
            content = render_score_dialog_snapshot(projection_snapshot);
            remember_score_dialog_render_with_signature(me, content,
                worker_state, signature);
            if (function_exists("record_owner_protocol_sync_fallback", me))
                me->record_owner_protocol_sync_fallback("score_dialog",
                    "owner read model not current");
        }
    }

    emit_score_dialog(me, content, worker_state, response_context);
    return 1;
}

int help(object me)
{
    msg_send(me, @HELP
命令：score（属性）

显示你的角色详细状态。

用法：
  score          - 显示完整状态

显示内容：
  - 基础属性：臂力、身法、根骨、悟性
  - 战斗属性：气血、内力
  - 成长概览：江湖阅历、成长档位、技能上限
  - 装备状态：当前武器

相关命令：
  skills         - 查看技能
  hp             - 查看生命状态
  inventory      - 查看物品
  equip          - 装备管理

HELP
    );
    return 1;
}
