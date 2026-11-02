/**
 * @file    xk_transport_d.c
 * @brief   XK fallback capability port for direct and driver gateway modes
 *
 * Direct and driver delivery are owned by the Go Gateway or the driver's
 * normal object output path. This service is only a capability marker; the
 * protocol core owns the direct fallback and no socket health is reported.
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

mapping query_capabilities()
{
    return ([
        "delivery_owner": "xk_protocol_core",
        "external_socket": 0,
        "mode": "direct_or_driver",
    ]);
}

void create()
{
    ::create();
}
