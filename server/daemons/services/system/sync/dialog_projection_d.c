/**
 * @file    dialog_projection_d.c
 * @brief   对话协议投影服务



 *
 * @par Description:
 * 统一收口 DLOG 类协议的公共 envelope 语义，当前先负责
 * source/dialog_id/provider_id/dialog_bucket 的拼装与合并。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

mapping build_dialog_extra(string source, string dialog_id, mapping extra)
{
    mapping payload;

    payload = mapp(extra) ? copy(extra) : ([]);
    if (stringp(source) && source != ""
        && (!stringp(payload["source"]) || payload["source"] == ""))
        payload["source"] = source;
    if (stringp(dialog_id) && dialog_id != ""
        && (!stringp(payload["dialog_id"]) || payload["dialog_id"] == ""))
        payload["dialog_id"] = dialog_id;

    return payload;
}

mapping build_provider_dialog_extra(string source, string dialog_id,
    string provider_id, string bucket, mapping extra)
{
    mapping payload;

    payload = build_dialog_extra(source, dialog_id, extra);
    if (stringp(provider_id) && provider_id != ""
        && (!stringp(payload["provider_id"]) || payload["provider_id"] == ""))
        payload["provider_id"] = provider_id;
    if (stringp(bucket) && bucket != ""
        && (!stringp(payload["dialog_bucket"]) || payload["dialog_bucket"] == ""))
        payload["dialog_bucket"] = bucket;

    return payload;
}
