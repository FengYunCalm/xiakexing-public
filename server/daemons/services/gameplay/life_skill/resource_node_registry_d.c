/**
 * @file    resource_node_registry_d.c
 * @brief   采集资源点注册表真源
 *
 * @note
 * 持有世界资源点的声明式配置，并输出 gather runtime 可直接消费的 target contract。
 * world 内容对象只引用资源点 id，不再各自内联 reward/exp/等级门槛。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

nosave mapping _nodes;

private mapping query_resource_node_config()
{
    mapping config;

    /* 资源点是区域包内容切片，聚合在 content 配置真源；
     * 旧的 /config/resource_node_config.c 从未存在，不做双源回退。 */
    config = CONFIG_D->query_full_config("content");
    return mapp(config) && mapp(config["resource_nodes"]) ? config : ([]);
}

private int validate_node(mapping node)
{
    if (!mapp(node))
        return 0;
    if (!stringp(node["skill_id"]) || node["skill_id"] == "")
        return 0;
    if (!stringp(node["reward_item"]) || node["reward_item"] == "")
        return 0;
    if (!ITEM_D->item_exists(node["reward_item"]))
        return 0;
    if (!intp(node["reward_count"]) || node["reward_count"] <= 0)
        return 0;
    if (!intp(node["exp_reward"]) || node["exp_reward"] <= 0)
        return 0;
    if (!intp(node["required_level"]) || node["required_level"] < 0)
        return 0;

    return 1;
}

private void load_node_config()
{
    mapping config;

    config = query_resource_node_config();
    _nodes = mapp(config["resource_nodes"]) ? copy(config["resource_nodes"]) : ([]);
}

void on_config_reloaded(string key, string config_name)
{
    /* resource_node 无独立配置注册（历史悬空回调已删除）；
     * 数据经 content 聚合段提供，content 回调即唯一重载通道。 */
    if (key != "content" && config_name != "content")
        return;

    load_node_config();
}

void on_config_reload_rollback(string key, string config_name)
{
    if (key != "content" && config_name != "content")
        return;
    load_node_config();
}

void create()
{
    ::create();

    if (!mapp(_nodes))
        _nodes = ([]);

    load_node_config();
    /* resource_node 无独立配置注册（历史悬空回调已删除）；
     * 数据经 content 聚合段提供，content 回调即唯一重载通道。 */
    CONFIG_D->compat_register_config_callback("content", "on_config_reloaded", this_object());
}

mapping query_node(string node_id)
{
    mapping node;

    if (!stringp(node_id) || node_id == "")
        return 0;

    node = _nodes[node_id];
    if (!validate_node(node))
        return 0;

    return copy(node);
}

mapping build_gather_target(string node_id, mapping contract)
{
    mapping node;
    mapping target;
    mapping fishing_contract;
    string contract_name;

    node = query_node(node_id);
    if (!mapp(node))
        return 0;

    target = copy(node);
    if (mapp(contract))
        target += copy(contract);

    /* 内容层可以声明活动专用 contract；运行时只消费已归一化的 target，
     * 不再把真实钓点降级成 synthetic target。 */
    contract_name = stringp(node["contract"]) && node["contract"] != ""
        ? node["contract"] : "synthetic_gather_target";
    if (stringp(target["contract"]) && target["contract"] != "")
        contract_name = target["contract"];
    target["contract"] = contract_name;

    fishing_contract = mapp(node["fishing_contract"])
        ? node["fishing_contract"] : ([]);
    if (contract_name == "fishing_contract" && sizeof(fishing_contract) > 0) {
        if (mapp(fishing_contract["catch_table"]))
            target["catch_table"] = copy(fishing_contract["catch_table"]);
        if (intp(fishing_contract["cooldown_seconds"]))
            target["cooldown_seconds"] = fishing_contract["cooldown_seconds"];
        if (mapp(fishing_contract["skill_exp"])
            && intp(fishing_contract["skill_exp"]["amount"]))
            target["exp_reward"] = fishing_contract["skill_exp"]["amount"];
    }

    target["target_id"] = stringp(target["target_id"]) && target["target_id"] != ""
        ? target["target_id"] : node_id;
    target["target_name"] = stringp(target["target_name"])
        && target["target_name"] != ""
        ? target["target_name"] : "资源点";

    return target;
}
