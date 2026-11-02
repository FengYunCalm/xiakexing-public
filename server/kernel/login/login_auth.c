/**
 * @file    login_auth.c
 * @brief   登录认证模块
 * @author  XiaKeXing Development Team
 * @date    2026-03-20
 * @version 1.0.2
 *
 * @par Description:
 * 提供用户名、中文名、性别和账号密码认证功能。
 */

#pragma strict_types

#include <system/globals.h>
#include <system/account.h>
#include <system/daemon.h>
#include <system/errors.h>

int valid_username(string username);
int valid_chinese_name(string name);
int valid_gender(string gender);
object account_d();
int authenticate_user(string username, string password);

private nosave object _player_service;

/**
 * @brief 验证用户名格式
 * @param username 用户名
 * @return 1=合法, 0=非法
 *
 * @par 规则:
 * - 长度2-32字符
 * - 仅允许字母、数字、下划线
 * - 防止路径遍历攻击
 */
int valid_username(string username)
{
    int i, len;
    int c;

    if (!username || username == "")
        return 0;

    len = strlen(username);
    if (len < ACCOUNT_MIN_USERNAME_LENGTH || len > ACCOUNT_MAX_USERNAME_LENGTH)
        return 0;

    for (i = 0; i < len; i++) {
        c = username[i];
        /* 只允许字母、数字、下划线 */
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_')) {
            return 0;
        }
    }
    return 1;
}

/**
 * @brief 验证中文名字
 * @param name 名字
 * @return 1=合法, 0=非法
 *
 * @par 规则:
 * - 角色名长度遵循 account.h 中的 CHARACTER_* 真源限制
 * - 仅允许中文字符
 */
int valid_chinese_name(string name)
{
    int i, len, char_count, codepoint;

    if (!name || name == "")
        return 0;

    /* 本驱动的字符串下标返回码位、strlen 按字符计数（非字节）：
     * 这里按码位判定，只接受 U+4E00..U+9FFF 的汉字。
     * 旧实现按 UTF-8 字节分支解码，在当前驱动上永远走不到。 */
    len = strlen(name);
    i = 0;
    char_count = 0;

    while (i < len) {
        codepoint = name[i];
        if (!intp(codepoint) || codepoint < 0x4E00 || codepoint > 0x9FFF)
            return 0;

        char_count++;
        if (char_count > CHARACTER_MAX_NAME_LENGTH)
            return 0;

        i++;
    }

    return char_count >= CHARACTER_MIN_NAME_LENGTH;
}

/**
 * @brief 验证性别
 * @param gender 性别字符串
 * @return 1=合法, 0=非法
 */
int valid_gender(string gender)
{
    if (!gender)
        return 0;

    return gender == "男" || gender == "女" ||
           lower_case(gender) == "male" || lower_case(gender) == "female" ||
           gender == "man" || gender == "woman";
}

object player_service()
{
    object service;

    if (objectp(_player_service))
        return _player_service;

    service = find_object(PLAYER_D);

    if (!service) {
        catch(service = load_object(PLAYER_D));
    }

    if (objectp(service))
        _player_service = service;

    return service;
}

/**
 * @brief 用户认证
 * @param username 用户名
 * @param password 密码
 * @return 1=验证成功, 0=密码错误, ERR_PLAYER_NOT_FOUND=用户不存在, 2=账号存在但需创建角色
 */
int authenticate_user(string username, string password)
{
    object accd;
    int password_valid;
    mixed err;

    if (!valid_username(username)) {
        log_file("system/security", sprintf("[LOGIN_AUTH] Authentication rejected: invalid username format '%s'\n", username));
        return 0;
    }

    if (!username || !password)
        return 0;

    err = catch {
        accd = player_service();
        if (!accd) {
            log_file("system/errors", sprintf("[LOGIN_AUTH] authenticate_user: player_service() daemon not available for user '%s'\n", username));
            return 0;
        }

        if (!accd->account_exists(username)) {
            log_file("system/security", sprintf("[LOGIN_AUTH] Authentication failed: user '%s' not found\n", username));
            return ERR_PLAYER_NOT_FOUND;
        }

        password_valid = accd->validate_password(username, password);
        if (password_valid != 1) {
            log_file("system/security", sprintf("[LOGIN_AUTH] Authentication failed: invalid password for user '%s'\n", username));
            return 0;
        }

        if (function_exists("account_has_character", accd)
            && !accd->account_has_character(username)) {
            log_file("system/auth", sprintf("[LOGIN_AUTH] User '%s' authenticated but character not created yet\n", username));
            return 2;
        }
    };

    if (err) {
        log_file("system/errors", sprintf("[LOGIN_AUTH] authenticate_user exception for user '%s': %s\n", username, err));
        return 0;
    }

    return 1;
}
