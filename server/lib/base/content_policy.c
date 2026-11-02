/**
 * @file    content_policy.c
 * @brief   区域内容包公共策略基类（单属主）
 *
 * @par Description:
 * 区域 package.c 继承本类获得公共装备：
 *  - query_section_value：区域切片文件加载与取值的统一容错入口。
 */

#pragma strict_types
#pragma save_types

nosave mapping _section_load_errors;

mapping query_all_config();

private void reset_section_load_errors()
{
    _section_load_errors = ([]);
}

private void record_section_load_error(string object_path, string function_name,
    string error_code, mixed error)
{
    string key;

    if (!mapp(_section_load_errors))
        _section_load_errors = ([]);
    key = stringp(object_path) ? object_path : "";
    _section_load_errors[key] = ([
        "source": key,
        "function": stringp(function_name) ? function_name : "",
        "error_code": stringp(error_code) ? error_code : "CONTENT_SECTION_LOAD_FAILED",
        "error_message": error ? sprintf("%O", error) : "section query failed",
    ]);
}

mapping query_config_status()
{
    mapping data;
    mixed err;

    reset_section_load_errors();
    err = catch(data = query_all_config());
    if (err)
        return ([
            "ok": 0,
            "error_code": "CONFIG_OBJECT_QUERY_FAILED",
            "error_message": sprintf("%O", err),
            "data": 0,
            "issues": ({ ([
                "source": file_name(this_object()),
                "error_code": "CONFIG_OBJECT_QUERY_FAILED",
                "error_message": sprintf("%O", err),
            ]) }),
        ]);

    if (sizeof(_section_load_errors))
        return ([
            "ok": 0,
            "error_code": "CONFIG_SECTION_LOAD_FAILED",
            "error_message": "one or more declared content sections failed to load",
            "data": 0,
            "issues": values(_section_load_errors),
        ]);

    if (!mapp(data) || sizeof(data) == 0)
        return ([
            "ok": 0,
            "error_code": "CONFIG_DATA_INVALID",
            "error_message": "query_all_config returned an empty mapping",
            "data": 0,
            "issues": ({ ([
                "source": file_name(this_object()),
                "error_code": "CONFIG_DATA_INVALID",
                "error_message": "query_all_config returned an empty mapping",
            ]) }),
        ]);

    return ([
        "ok": 1,
        "source": file_name(this_object()),
        "data": copy(data),
        "issues": ({ }),
    ]);
}

protected mixed query_section_value(string object_path, string function_name)
{
    object section_ob;
    object stale_ob;
    mixed value;
    mixed err;

    if (!stringp(object_path) || object_path == ""
        || !stringp(function_name) || function_name == "") {
        record_section_load_error(object_path, function_name,
            "CONTENT_SECTION_PATH_INVALID", 0);
        return ([]);
    }

    /* Content leaves are data-only objects. Destructing an already loaded leaf
     * before reload makes a new package generation observe the new program,
     * instead of merely reusing the old LPC object. */
    stale_ob = find_object(object_path);
    if (objectp(stale_ob))
        destruct(stale_ob);

    err = catch(section_ob = load_object(object_path));
    if (err || !objectp(section_ob)) {
        record_section_load_error(object_path, function_name,
            "CONTENT_SECTION_LOAD_FAILED", err);
        return ([]);
    }
    err = catch(value = call_other(section_ob, function_name));
    if (err) {
        record_section_load_error(object_path, function_name,
            "CONTENT_SECTION_QUERY_FAILED", err);
        return ([]);
    }
    if (mapp(value))
        return copy(value);
    if (arrayp(value))
        return value + ({ });
    return value;
}
