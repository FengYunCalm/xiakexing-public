/**
 * @file    heartbeat_optimizer.c
 * @brief   living 侧心跳 profile/metrics facade
 * @author  XiaKeXing Development Team
 * @date    2026-03-20
 * @version 1.1.0
 *
 * @par Description:
 * 为 living 家族暴露 profile 与 metrics 读取能力。
 * 第一刀只负责对象侧 profile 描述和基础观测，不直接改动原生 `heart_beat()` 调度策略。
 */

#pragma strict_types

nosave mapping _heartbeat_profile;

mapping query_batched_heartbeat_handlers();

void set_heartbeat_profile(string profile_id, mapping overrides)
{
    mapping profile;

    profile = ([
        "id": stringp(profile_id) && profile_id != "" ? profile_id : "default",
        "interval": 1,
        "batch": function_exists("query_heartbeat_batch_name", this_object())
            ? call_other(this_object(), "query_heartbeat_batch_name")
            : replace_string(base_name(this_object()), "/", "."),
        "enabled": 1,
    ]);
    if (mapp(overrides))
        profile += copy(overrides);
    if (!intp(profile["interval"]) || profile["interval"] < 1)
        profile["interval"] = 1;
    if (!stringp(profile["batch"]) || profile["batch"] == "")
        profile["batch"] = replace_string(base_name(this_object()), "/", ".");

    _heartbeat_profile = profile;
}

mapping query_heartbeat_profile()
{
    if (!mapp(_heartbeat_profile))
        set_heartbeat_profile("default", ([]));

    return copy(_heartbeat_profile);
}

mapping query_heartbeat_metrics()
{
    mapping profile;

    profile = query_heartbeat_profile();
    return ([
        "profile": profile["id"] || "default",
        "interval": profile["interval"] || 1,
        "batch": profile["batch"] || "default",
        "native_heartbeat": query_heart_beat(this_object()),
        "batched_handlers": sizeof(query_batched_heartbeat_handlers()),
    ]);
}
