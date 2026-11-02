/**
 * @file    economy_auction_settlement.c
 * @brief   拍卖保证金、佣金与成交关闭结算模块。
 */

#pragma strict_types
#pragma save_types

float normalize_rate(mixed raw_rate, float fallback);
mapping query_economy_auction_config();
mapping build_result(int success, string message, mapping extra);

float query_auction_deposit_rate()
{
    mapping auction_config;

    auction_config = query_economy_auction_config();
    return normalize_rate(auction_config["deposit_rate"], 0.0);
}

float query_auction_commission_rate()
{
    mapping auction_config;

    auction_config = query_economy_auction_config();
    return normalize_rate(auction_config["commission_rate"], 0.0);
}

int *query_auction_duration_bounds()
{
    mapping auction_config;
    int duration_max;
    int duration_min;

    auction_config = query_economy_auction_config();
    duration_min = intp(auction_config["duration_min"]) ? auction_config["duration_min"] : 3600;
    duration_max = intp(auction_config["duration_max"]) ? auction_config["duration_max"] : 604800;
    if (duration_min <= 0)
        duration_min = 3600;
    if (duration_max < duration_min)
        duration_max = duration_min;

    return ({ duration_min, duration_max });
}

mapping query_auction_runtime_snapshot()
{
    int *bounds;

    bounds = query_auction_duration_bounds();
    return ([
        "enabled": query_economy_auction_config()["enabled"] ? 1 : 0,
        "deposit_rate": query_auction_deposit_rate(),
        "commission_rate": query_auction_commission_rate(),
        "duration_min": bounds[0],
        "duration_max": bounds[1],
    ]);
}

int query_auction_deposit(int price)
{
    if (!intp(price) || price <= 0)
        return 0;

    return to_int(price * query_auction_deposit_rate());
}

int query_auction_commission(int price)
{
    if (!intp(price) || price <= 0)
        return 0;

    return to_int(price * query_auction_commission_rate());
}

mapping settle_auction_listing_close(mapping listing)
{
    int commission;
    int current_bid;
    int deposit;

    if (!mapp(listing))
        return build_result(0, "拍卖结算失败。\n", 0);

    current_bid = intp(listing["current_bid"]) ? listing["current_bid"] : 0;
    deposit = intp(listing["deposit"]) ? listing["deposit"] : 0;
    commission = query_auction_commission(current_bid);
    if (commission < 0)
        commission = 0;

    if (!stringp(listing["current_bidder"]) || listing["current_bidder"] == "") {
        return build_result(1, "", ([
            "action": "auction_close",
            "listing_id": listing["id"] || "",
            "sold": 0,
            "seller_gold": deposit,
            "commission": 0,
            "deposit": deposit,
        ]));
    }

    return build_result(1, "", ([
        "action": "auction_close",
        "listing_id": listing["id"] || "",
        "sold": 1,
        "seller_gold": current_bid - commission + deposit,
        "commission": commission,
        "deposit": deposit,
        "current_bid": current_bid,
        "current_bidder": listing["current_bidder"] || "",
    ]));
}
