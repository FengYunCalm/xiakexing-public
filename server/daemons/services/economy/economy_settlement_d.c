/**
 * @file    economy_settlement_d.c
 * @brief   经济结算核心真链门面
 *
 * @par Description:
 * 对外保持 ECONOMY_SETTLEMENT_D 单一入口；内部按通用配置、商店、银行、交易、拍卖、住房、奖励倍率和运行快照拆分。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";
inherit "/daemons/services/economy/economy_settlement_common";
inherit "/daemons/services/economy/economy_reward_settlement";
inherit "/daemons/services/economy/economy_housing_settlement";
inherit "/daemons/services/economy/economy_shop_settlement";
inherit "/daemons/services/economy/economy_trade_settlement";
inherit "/daemons/services/economy/economy_auction_settlement";
inherit "/daemons/services/economy/economy_bank_settlement";
inherit "/daemons/services/economy/economy_death_settlement";
inherit "/daemons/services/economy/economy_guild_settlement";
inherit "/daemons/services/economy/economy_runtime_snapshot";

mapping query_economy_owner_route(string key)
{
    mapping route;
    string route_key;
    mixed err;

    route_key = stringp(key) && key != "" ? key : "settlement";
    err = catch(route = OWNER_RUNTIME_D->query_service_owner_route("economy", route_key));
    if (err || !mapp(route) || route["ok"] != 1)
        return ([
            "ok": 0,
            "service": "economy",
            "key": route_key,
            "failure_reason": err ? sprintf("owner route query failed: %O", err)
                : (mapp(route) ? route["failure_reason"] : "owner route unavailable"),
            "readiness": mapp(route) ? route["readiness"] : ([ "ok": 0 ]),
            "dependencies": mapp(route) ? route["dependencies"] : ([ ]),
            "direct_cross_owner_write": 0,
        ]);

    route["commit_boundary"] = "proposal_or_service_owner_commit";
    route["direct_cross_owner_write"] = 0;
    return route;
}

mapping query_economy_owner_status()
{
    mapping route;

    route = query_economy_owner_route("settlement");
    if (!mapp(route) || route["ok"] != 1)
        return route;
    return ([
        "service_map_v2": 1,
        "service": "economy",
        "policy": route["policy"],
        "owner_id": route["owner_id"],
        "commit_boundary_required": 1,
        "commit_boundary": route["commit_boundary"],
        "service_shard_owner": route["service_shard_owner"] ? 1 : 0,
        "service_shard_key_required": route["service_shard_key_required"] ? 1 : 0,
        "keyed_service_shard_ready": route["keyed_service_shard_ready"] ? 1 : 0,
        "hot_path_service_owner_single_point": 0,
        "message_only_cross_owner": 1,
        "direct_cross_owner_write": 0,
    ]);
}
