/**
 * @file    simul_efun_bootstrap.c
 * @brief   simul_efun helper 预加载辅助子服务
 * @author  XiaKeXing Development Team
 * @date    2026-03-30
 * @version 1.0.0
 */

#pragma strict_types

#include <system/services.h>

void preload_helpers(string *helper_paths)
{
    string helper_path;

    if (!arrayp(helper_paths))
        return;

    foreach (helper_path in helper_paths) {
        if (!stringp(helper_path) || helper_path == "")
            continue;
        load_object(helper_path);
    }
}
