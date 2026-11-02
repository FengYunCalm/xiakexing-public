/**
 * @file    economy_settlement_common.c
 * @brief   经济结算通用配置、倍率和结果 helper。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

mapping query_economy_config()
{
    mapping config;

    config = CONFIG_D->query_full_config("economy");
    return mapp(config) ? config : ([]);
}

mapping query_economy_bank_config()
{
    mapping config;

    config = query_economy_config();
    return mapp(config) && mapp(config["bank"]) ? config["bank"] : ([]);
}

mapping query_economy_shop_config()
{
    mapping config;

    config = query_economy_config();
    return mapp(config) && mapp(config["shop"]) ? config["shop"] : ([]);
}

mapping query_economy_trading_config()
{
    mapping config;

    config = query_economy_config();
    return mapp(config) && mapp(config["trading"]) ? config["trading"] : ([]);
}

mapping query_economy_auction_config()
{
    mapping config;

    config = query_economy_config();
    return mapp(config) && mapp(config["auction"]) ? config["auction"] : ([]);
}

mapping query_economy_housing_config()
{
    mapping config;

    config = query_economy_config();
    return mapp(config) && mapp(config["housing"]) ? config["housing"] : ([]);
}

float normalize_rate(mixed raw_rate, float fallback)
{
    float rate;

    if (!(intp(raw_rate) || floatp(raw_rate)))
        return fallback;

    rate = to_float(raw_rate);
    if (rate < 0.0)
        return fallback;
    if (rate > 1.0)
        rate = rate / 100.0;

    return rate;
}

int apply_price_rate(int base_price, float rate)
{
    int price;

    if (!intp(base_price) || base_price <= 0 || rate < 0.0)
        return 0;

    price = to_int(base_price * rate);
    if (price <= 0 && rate > 0.0)
        return 1;

    return price;
}

float normalize_multiplier(mixed raw_multiplier, float fallback)
{
    float multiplier;

    if (!(intp(raw_multiplier) || floatp(raw_multiplier)))
        return fallback;

    multiplier = to_float(raw_multiplier);
    if (multiplier < 0.0)
        return fallback;

    return multiplier;
}

mapping build_result(int success, string message, mapping extra)
{
    mapping result;

    result = ([
        "success": success ? 1 : 0,
        "message": stringp(message) ? message : "",
    ]);
    if (mapp(extra))
        result += extra;
    return result;
}

/* 统一构造资金 operation spec（operation id 唯一来源）。
 * 调用方只提供业务参数（domain/action/actor/subject/sequence），
 * 不自行拼 id 或维护第二套 pending schema。返回 spec 供
 * MONEY_D->apply_money_operation / queue_money_compensation 消费。 */
nosave int _settlement_operation_sequence;
nosave string _settlement_operation_epoch;

/* 构造资金 operation spec。显式 operation_id 是跨重试的唯一身份；
 * sequence 仅兼容仍由调用方提供序号的旧入口，禁止把裸时间戳当作身份。 */
mapping build_money_operation_spec(string domain, string action,
    string actor_id, string subject_id, int sequence)
{
    return ([
        "domain": domain,
        "action": action,
        "actor_id": actor_id,
        "subject_id": subject_id,
        "sequence": sequence,
        "operation_id": sprintf("%s_%s_%s_%s_%d", domain, action,
            actor_id, subject_id, sequence),
    ]);
}

mapping build_money_operation_spec_with_id(string domain, string action,
    string actor_id, string subject_id, string operation_id)
{
    mapping spec;

    if (!stringp(operation_id) || operation_id == "")
        return 0;
    spec = ([
        "domain": domain,
        "action": action,
        "actor_id": actor_id,
        "subject_id": subject_id,
        "operation_id": operation_id,
    ]);
    return spec;
}

/* 新实例使用独立随机命名空间，避免同秒重载复用已入账的身份。
 * 重试方必须保存并复用返回值，不能再次生成新操作。 */
string issue_money_operation_id(string domain, string action,
    string actor_id, string subject_id)
{
    if (!_settlement_operation_epoch)
        _settlement_operation_epoch = sprintf("%016x%016x%016x",
            secure_random(MAX_INT), secure_random(MAX_INT), secure_random(MAX_INT));
    _settlement_operation_sequence++;
    return sprintf("%s/%s/%s/%s/%s/%d", domain, action,
        actor_id || "", subject_id || "", _settlement_operation_epoch,
        _settlement_operation_sequence);
}

/* 所有业务 producer 的资金边界。业务层可以提供稳定 operation_id；
 * 留空时由结算门面生成一次新调用身份，重试方不得重新生成。 */
varargs mapping settle_money_operation(object player, string domain,
    string action, string direction, int amount, string operation_id)
{
    mapping spec;
    string actor_id;

    if (!objectp(player) || amount <= 0
        || (direction != "add" && direction != "deduct"))
        return ([ "ok": 0, "state": "invalid_args" ]);
    actor_id = PLAYER_STATE_D->query_player_id(player);
    if (!stringp(operation_id) || operation_id == "")
        operation_id = issue_money_operation_id(domain, action, actor_id,
            "player");
    spec = build_money_operation_spec_with_id(domain, action, actor_id,
        "player", operation_id);
    spec["direction"] = direction;
    spec["amount"] = amount;
    return MONEY_D->apply_money_operation(player, spec);
}

/* 补偿失败进可恢复队列（调用方一行调用，不自行拼 id）。
 * 内部经 build_money_operation_spec 构造 operation id 后入队；
 * 重试必须复用同一 operation_id，队列不再为同一业务键追加后缀。 */
int queue_compensation(object who, string domain, string action,
    string actor_id, string subject_id, int sequence, int amount)
{
    mapping spec;

    if (!objectp(who) || amount <= 0)
        return 0;
    if (sequence > 0)
        spec = build_money_operation_spec(domain, action, actor_id,
            subject_id, sequence);
    else
        spec = build_money_operation_spec_with_id(domain, action, actor_id,
            subject_id, issue_money_operation_id(domain, action,
                actor_id, subject_id) + "/refund");
    spec["amount"] = amount;
    spec["direction"] = "add";
    return MONEY_D->queue_money_compensation(who, spec);
}

int queue_compensation_with_operation(object who, string domain, string action,
    string actor_id, string subject_id, string operation_id, int amount)
{
    mapping spec;

    if (!objectp(who) || amount <= 0 || !stringp(operation_id)
        || operation_id == "")
        return 0;
    spec = build_money_operation_spec_with_id(domain, action, actor_id,
        subject_id, operation_id);
    spec["amount"] = amount;
    spec["direction"] = "add";
    return MONEY_D->queue_money_compensation(who, spec);
}
