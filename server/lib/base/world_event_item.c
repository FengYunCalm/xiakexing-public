#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/base/item/item";

nosave string _event_id;

void create()
{
    ::create();
    set("world_event_object", 1);
}

int apply_event_context(string event_id, mapping event)
{
    mapping entity;
    string template_id;

    if (!stringp(event_id) || event_id == "" || !mapp(event))
        return 0;
    template_id = stringp(event["template_id"]) ? event["template_id"] : "";
    if (template_id == "")
        return 0;

    entity = ([
        "template_id": template_id,
        "present_id": event["present_id"] || event_id,
        "amount": intp(event["amount"]) && event["amount"] > 0
            ? event["amount"] : 1,
    ]);
    if (!apply_content_entity(entity))
        return 0;

    /* 一次性奇遇必须保持独立物品，避免入包时与旧堆叠合并后在持久化失败
     * 回滚时无法恢复原始地面对象。 */
    if (event["single_use"] == 1) {
        set("stackable", 0);
        set("max_stack", 1);
    }
    _event_id = event_id;
    set("world_event_object", 1);
    set("world_event_id", event_id);
    return 1;
}
