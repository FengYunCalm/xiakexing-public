/**
 * @file    encumbrance.c
 * @brief   负重子模块



 *
 * @par Description:
 * 基础对象层子模块，供上层对象组合能力。
 */

#pragma strict_types

mixed query(string path);

int query_max_encumbrance()
{
    int strength = query("attributes/strength") || 10;
    return strength * 1000;
}

int query_encumbrance()
{
    object *inventory = all_inventory(this_object());
    int encumbrance = 0;
    int index;
    int inventory_size = sizeof(inventory);

    for (index = 0; index < inventory_size; index++) {
        if (objectp(inventory[index])) {
            encumbrance += inventory[index]->query_weight() || 1;
        }
    }

    return encumbrance;
}

int is_over_encumbered()
{
    return query_encumbrance() > query_max_encumbrance();
}
