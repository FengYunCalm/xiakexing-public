/**
 * @file    command.h
 * @brief   命令系统头文件 - Command System Header



 *
 * @par Description:
 * 命令目录常量与少量兼容声明。
 * 消息发送不以本头文件为真源，业务层统一直接走 msg_send 家族。
 *
 */
#pragma strict_types

#ifndef __GAME_COMMAND_H__
#define __GAME_COMMAND_H__

#include <system/globals.h>
#define CMD_DIR         "/cmds/player/"
#define ADMIN_CMD_DIR   "/cmds/admin/"


// 命令执行
int command_hook(string input);

#endif // __GAME_COMMAND_H__
