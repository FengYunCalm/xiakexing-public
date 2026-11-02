/*
 * room payload 助手路径单属主（七常量）：ROOM_{TITLE,EXITS,MARKER,
 * MAPGRID,MAPREAD}_XK_HELPER 五条直定义供拆分文件互相调用，
 * SIMUL_EFUN_ROOM_*_XK_HELPER 五条别名 + ENTITIES/SYNC 两条直定义
 * 供 kernel/simul_efun.c 门面与 helper 注册表使用。此前以本地
 * #define 散布各文件，漏带即编译期零告警、运行期以零参闭包形态
 * 爆炸（本引擎将未定义大写标识符编译为 closure）。新增使用方只需
 * include 本头，不再允许本地重定义；helper 文件改名/移动只改此处。
 */
#ifndef __SYSTEM_ROOM_META_HELPERS_H
#define __SYSTEM_ROOM_META_HELPERS_H

#define ROOM_TITLE_XK_HELPER "/kernel/simul_efun_room_title_xk"
#define ROOM_EXITS_XK_HELPER "/kernel/simul_efun_room_exits_xk"
#define ROOM_MARKER_XK_HELPER "/kernel/simul_efun_room_marker_xk"
#define ROOM_MAPGRID_XK_HELPER "/kernel/simul_efun_room_mapgrid_xk"
#define ROOM_MAPREAD_XK_HELPER "/kernel/simul_efun_room_mapread_xk"

#define SIMUL_EFUN_ROOM_TITLE_XK_HELPER ROOM_TITLE_XK_HELPER
#define SIMUL_EFUN_ROOM_EXITS_XK_HELPER ROOM_EXITS_XK_HELPER
#define SIMUL_EFUN_ROOM_MARKER_XK_HELPER ROOM_MARKER_XK_HELPER
#define SIMUL_EFUN_ROOM_MAPGRID_XK_HELPER ROOM_MAPGRID_XK_HELPER
#define SIMUL_EFUN_ROOM_MAPREAD_XK_HELPER ROOM_MAPREAD_XK_HELPER
#define SIMUL_EFUN_ROOM_ENTITIES_XK_HELPER "/kernel/simul_efun_room_entities_xk"
#define SIMUL_EFUN_ROOM_SYNC_XK_HELPER "/kernel/simul_efun_room_sync_xk"

#endif
