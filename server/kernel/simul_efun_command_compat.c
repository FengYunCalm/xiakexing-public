/**
 * @file    simul_efun_command_compat.c
 * @brief   命令兼容辅助子服务
 * @author  XiaKeXing Development Team
 * @date    2026-03-30
 * @version 1.0.0
 */

#pragma strict_types

protected void force_me(string cmd)
{
    if (!stringp(cmd) || cmd == "")
        return;

    command(cmd);
}
