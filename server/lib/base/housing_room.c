#pragma strict_types

/**
 * @file    housing_room.c
 * @brief   Dynamic housing instance room identity and configuration hooks.
 */

inherit "/lib/base/room";

private string _housing_property_id;
private string _housing_owner_id;

void create()
{
    ::create();
    _housing_property_id = "";
    _housing_owner_id = "";
    set("housing/room", 1);
}

int configure_housing_room(mapping property_def, string property_id,
    string owner_id)
{
    if (!mapp(property_def) || !stringp(property_id) || property_id == ""
        || !stringp(owner_id) || owner_id == "")
        return 0;

    _housing_property_id = property_id;
    _housing_owner_id = owner_id;
    set("housing/property_id", property_id);
    set("housing/owner_id", owner_id);
    set("housing/property_def", copy(property_def));
    set("short", property_def["name"] || property_id);
    set("zone", "housing");
    set_no_fight(1);
    return 1;
}

string query_housing_property_id()
{
    return _housing_property_id || "";
}

string query_housing_owner_id()
{
    return _housing_owner_id || "";
}

int is_housing_room()
{
    return 1;
}
