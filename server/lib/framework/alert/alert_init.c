/**
 * @file    alert_init.c
 * @brief   告警初始化子模块



 *
 * @par Description:
 * 告警系统内部子模块。
 * 兼容 forwarding shell，保留旧拆分入口名，但初始化真源在 alert_system.c。
 */

#pragma strict_types
#pragma save_types

#define ALERT_SYSTEM "/lib/framework/alert_system"

void alert_init()
{
    /* 这里只转发，不持有任何本地初始化状态。 */
    call_other(ALERT_SYSTEM, "alert_init");
}

void create()
{
    alert_init();
}
