/**
 * @file    economy_runtime_snapshot.c
 * @brief   经济结算运行时配置快照模块。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

#define ECONOMY_DEATH_SETTLEMENT_HELPER "/daemons/services/economy/economy_death_settlement"

mapping query_economy_config();
float query_bank_transaction_fee_rate();
float query_shop_buy_rate();
float query_shop_sell_rate();
float query_shop_price_fluctuation();
int query_housing_default_rent_days();
int query_housing_default_tax_days();
int query_housing_tax_grace_days();
mapping query_death_penalty_snapshot();
float normalize_rate(mixed raw_rate, float fallback);
float normalize_multiplier(mixed raw_multiplier, float fallback);

mapping query_economy_runtime_snapshot()
{
    mapping config;
    mapping auction_config;
    mapping bank_config;
    mapping housing_config;
    mapping reward_config;
    mapping shop_config;
    mapping trading_config;

    config = query_economy_config();
    bank_config = mapp(config) && mapp(config["bank"]) ? config["bank"] : ([]);
    housing_config = mapp(config) && mapp(config["housing"]) ? config["housing"] : ([]);
    reward_config = mapp(config) && mapp(config["rewards"]) ? config["rewards"] : ([]);
    shop_config = mapp(config) && mapp(config["shop"]) ? config["shop"] : ([]);
    trading_config = mapp(config) && mapp(config["trading"]) ? config["trading"] : ([]);
    auction_config = mapp(config) && mapp(config["auction"]) ? config["auction"] : ([]);

    return ([
        "config_name": "economy",
        "loaded_config_ok": sizeof(keys(config)) > 0,
        "bank": ([
            "transaction_fee_rate": query_bank_transaction_fee_rate(),
        ]),
        "shop": ([
            "buy_rate": query_shop_buy_rate(),
            "sell_rate": query_shop_sell_rate(),
            "price_fluctuation": query_shop_price_fluctuation(),
            "demand_factor": shop_config["demand_factor"] ? 1 : 0,
            "restock_interval": intp(shop_config["restock_interval"]) ? shop_config["restock_interval"] : 0,
        ]),
        "trading": ([
            "enabled": trading_config["enabled"] ? 1 : 0,
            "tax_rate": intp(trading_config["tax_rate"]) ? trading_config["tax_rate"] : 0,
            "min_trade_skill_cap": intp(trading_config["min_trade_skill_cap"]) ? trading_config["min_trade_skill_cap"] : 0,
            "trade_timeout": intp(trading_config["trade_timeout"]) ? trading_config["trade_timeout"] : 0,
            "scope": "same_room",
        ]),
        "auction": ([
            "enabled": auction_config["enabled"] ? 1 : 0,
            "duration_min": intp(auction_config["duration_min"]) ? auction_config["duration_min"] : 0,
            "duration_max": intp(auction_config["duration_max"]) ? auction_config["duration_max"] : 0,
            "deposit_rate": normalize_rate(auction_config["deposit_rate"], 0.0),
            "commission_rate": normalize_rate(auction_config["commission_rate"], 0.0),
        ]),
        "housing": ([
            "enabled": housing_config["enabled"] ? 1 : 0,
            "default_rent_days": query_housing_default_rent_days(),
            "default_tax_days": query_housing_default_tax_days(),
            "tax_grace_days": query_housing_tax_grace_days(),
        ]),
        "death_penalty": call_other(ECONOMY_DEATH_SETTLEMENT_HELPER,
            "query_death_penalty_snapshot"),
        "rewards": ([
            "gold_multiplier": normalize_multiplier(reward_config["gold_multiplier"], 1.0),
            "quest_gold_multiplier": normalize_multiplier(reward_config["quest_gold_multiplier"], 1.0),
            "kill_gold_multiplier": normalize_multiplier(reward_config["kill_gold_multiplier"], 1.0),
            "dungeon_gold_multiplier": normalize_multiplier(reward_config["dungeon_gold_multiplier"], 1.0),
        ]),
        "sinks": ([
            "heal": MEDICAL_D->query_medical_runtime_snapshot(),
            "repair": ITEM_REPAIR_D->query_repair_runtime_snapshot(),
        ]),
    ]);
}
