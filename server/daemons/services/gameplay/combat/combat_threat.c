#pragma strict_types
#pragma save_types

/**
 * @file    combat_threat.c
 * @brief   多人战斗仇恨值计算（纯函数层，由 COMBAT_D 持有仇恨表）。
 *
 * 规则（多人战斗设计）：
 * - 伤害仇恨：造成每 1 点伤害记 1 点仇恨（治疗/护体仇恨预留，后续扩展）；
 * - 先手仇恨：战斗建立时双方互记 100 基础仇恨，保证 1v1 行为不变；
 * - 衰减：每回合仇恨 ×95%（长战不无限堆积，1v1 时因只有单一目标无行为差异）；
 * - 目标选择：NPC 行动时选择敌对侧仇恨最高的存活者；1v1 时唯一目标=原对手。
 */

#define THREAT_BASE_INIT 100
#define THREAT_DECAY_BP  9500

/* 初始仇恨表：双方互记基础仇恨 */
mapping init_combat_threat(object attacker, object defender)
{
    mapping threat;

    threat = ([ ]);
    if (!objectp(attacker) || !objectp(defender))
        return threat;
    threat[attacker] = ([ defender: THREAT_BASE_INIT ]);
    threat[defender] = ([ attacker: THREAT_BASE_INIT ]);
    return threat;
}

/* 新参与者加入：向敌对侧每人记基础仇恨，敌对侧向新参与者记基础仇恨 */
void seed_join_threat(mapping threat, object participant, object *hostiles)
{
    object hostile;

    if (!mapp(threat) || !objectp(participant))
        return;
    if (!mapp(threat[participant]))
        threat[participant] = ([ ]);
    foreach (hostile in hostiles) {
        if (!objectp(hostile) || hostile == participant)
            continue;
        threat[participant][hostile] =
            (intp(threat[participant][hostile]) ? threat[participant][hostile] : 0)
            + THREAT_BASE_INIT;
        if (!mapp(threat[hostile]))
            threat[hostile] = ([ ]);
        threat[hostile][participant] =
            (intp(threat[hostile][participant]) ? threat[hostile][participant] : 0)
            + THREAT_BASE_INIT;
    }
}

/* 伤害仇恨：source 对 target 的仇恨累加（amount 为实际伤害） */
int record_combat_threat(mapping threat, object source, object target, int amount)
{
    int current;

    if (!mapp(threat) || !objectp(source) || !objectp(target) || amount <= 0)
        return 0;
    if (!mapp(threat[source]))
        threat[source] = ([ ]);
    current = intp(threat[source][target]) ? threat[source][target] : 0;
    threat[source][target] = current + amount;
    return threat[source][target];
}

/* 每回合衰减：所有仇恨 ×95% */
void decay_combat_threat(mapping threat)
{
    object source;
    object target;

    if (!mapp(threat))
        return;
    foreach (source in keys(threat)) {
        if (!mapp(threat[source]))
            continue;
        foreach (target in keys(threat[source])) {
            threat[source][target] =
                (intp(threat[source][target]) ? threat[source][target] : 0)
                * THREAT_DECAY_BP / 10000;
        }
    }
}

/* 目标选择：actor 对 hostiles 中仇恨最高的存活者（0 表示无目标） */
object resolve_threat_target(mapping threat, object actor, object *hostiles)
{
    mapping source_hatred;
    object best;
    object hostile;
    int best_threat;

    if (!mapp(threat) || !objectp(actor) || !arrayp(hostiles) || sizeof(hostiles) == 0)
        return 0;
    source_hatred = mapp(threat[actor]) ? threat[actor] : ([ ]);
    best_threat = -1;
    foreach (hostile in hostiles) {
        if (!objectp(hostile) || hostile == actor)
            continue;
        if ((hostile->query("combat/hp") || 0) <= 0)
            continue;
        if (intp(source_hatred[hostile]) && source_hatred[hostile] > best_threat) {
            best_threat = source_hatred[hostile];
            best = hostile;
        }
    }
    return best;
}
