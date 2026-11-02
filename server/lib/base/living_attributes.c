/**
 * @file    living_attributes.c
 * @brief   活体属性子模块



 *
 * @par Description:
 * 基础对象层子模块，供上层对象组合能力。
 */

#pragma strict_types

mixed query(string path);

private int query_base_attribute(string canonical_path, string legacy_path, int default_value)
{
    mixed value;

    value = query(canonical_path);
    if (intp(value) && value > 0)
        return value;

    /*
     * living 基类优先读取嵌套属性真源；只有在老存档尚未完全清理前，
     * 才回退到扁平字段，避免基础能力接口把旧路径重新扶正为主路径。
     */
    value = query(legacy_path);
    if (intp(value) && value > 0)
        return value;

    return default_value;
}

int query_per()
{
    /*
     * 默认值与 attribute service 保持一致，避免同一个角色在“基类直查”与
     * “attribute_d 汇总查询”之间出现两套 perception 基线。
     */
    return query_base_attribute("attributes/perception", "per", 10);
}

int query_dex()
{
    return query_base_attribute("attributes/dexterity", "dex", 10);
}

int query_con()
{
    return query_base_attribute("attributes/constitution", "con", 10);
}
