/**
 * @file    quest_catalog.c
 * @brief   任务定义目录辅助子服务
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

private void merge_chunk(mapping defs, mapping chunk)
{
    string quest_id;
    mapping quest_def;

    if (!mapp(defs) || !mapp(chunk))
        return;

    foreach (quest_id, quest_def in chunk) {
        if (stringp(quest_id) && quest_id != "" && mapp(quest_def))
        {
            if (mapp(defs[quest_id]))
                error(sprintf("duplicate quest definition id: %s", quest_id));
            defs[quest_id] = copy(quest_def);
        }
    }
}

mapping build_default_quest_definitions()
{
    mapping defs;
    mapping content_defs;

    defs = ([]);
    content_defs = CONFIG_D->query_config("content", "quest_definitions");
    merge_chunk(defs, content_defs);
    return defs;
}

mapping get_all_quests_info(mapping quest_definitions)
{
    return copy(quest_definitions || ([]));
}

mapping get_quests_by_type(mapping quest_definitions, string type)
{
    mapping result;
    string quest_id;
    mapping quest_info;

    result = ([]);
    if (!stringp(type) || !mapp(quest_definitions))
        return result;

    foreach (quest_id, quest_info in quest_definitions) {
        if (mapp(quest_info) && quest_info["type"] == type)
            result[quest_id] = copy(quest_info);
    }

    return result;
}

mapping get_quests_by_provider(mapping quest_definitions, string provider_id)
{
    mapping result;
    string quest_id;
    mapping quest_info;

    result = ([]);
    if (!stringp(provider_id) || provider_id == "")
        return result;

    foreach (quest_id, quest_info in quest_definitions) {
        if (mapp(quest_info) && quest_info["provider"] == provider_id)
            result[quest_id] = copy(quest_info);
    }

    return result;
}

mapping get_quest_definition(mapping quest_definitions, string quest_id)
{
    if (!stringp(quest_id) || !mapp(quest_definitions))
        return 0;

    return copy(quest_definitions[quest_id]);
}

int quest_exists(mapping quest_definitions, string quest_id)
{
    return mapp(get_quest_definition(quest_definitions, quest_id));
}
