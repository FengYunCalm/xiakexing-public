/* player_lifecycle.h —— 玩家生命周期 changed-domain 固定词汇。
 * dispatcher 只接受本集合内的 domain；新增 domain 必须同步本文件。
 * 常量语义（与 XK 协议投影 schema 对齐）：
 *   inventory/status/quest/money/world 对应协议 IVTY/STAT/QUEST 同步域、
 *   货币结算域与房间/地图投影域。 */
#ifndef PLAYER_LIFECYCLE_H
#define PLAYER_LIFECYCLE_H

#define LIFECYCLE_DOMAIN_INVENTORY "inventory"
#define LIFECYCLE_DOMAIN_STATUS    "status"
#define LIFECYCLE_DOMAIN_QUEST     "quest"
#define LIFECYCLE_DOMAIN_MONEY     "money"
#define LIFECYCLE_DOMAIN_WORLD     "world"

#define LIFECYCLE_DOMAINS_ALL      ({ LIFECYCLE_DOMAIN_INVENTORY, \
    LIFECYCLE_DOMAIN_STATUS, LIFECYCLE_DOMAIN_QUEST, \
    LIFECYCLE_DOMAIN_MONEY, LIFECYCLE_DOMAIN_WORLD })

#endif
