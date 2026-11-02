/**
 * @file    password_hash_d.c
 * @brief   账号密码哈希服务
 *
 * 所有账号使用当前 FluffOS crypt() 支持的 bcrypt 格式。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>

inherit "/lib/framework/daemon";

#define BCRYPT_PREFIX "$2y$12$"
/* bcrypt 只使用前 72 字节：超长输入会被静默截断，使不同密码共享同一 hash。
 * 新凭据按字节上限拒绝；既有凭据的校验保持兼容（不因长度重算或迁移）。 */
#define BCRYPT_MAX_PASSWORD_BYTES 72
#define BCRYPT_SALT_CHARS "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"

private string generate_bcrypt_salt();
int utf8_byte_length(string text);
int query_utf8_byte_length(string text);

void create()
{
    ::create();
}

private string generate_bcrypt_salt()
{
    int index;
    string salt;

    salt = "";
    for (index = 0; index < 22; index++)
        salt += sprintf("%c", BCRYPT_SALT_CHARS[
            random(strlen(BCRYPT_SALT_CHARS))]);
    return salt;
}

string hash_password(string password)
{
    string result;
    mixed error;

    if (!stringp(password) || password == "")
        return "";
    if (utf8_byte_length(password) > BCRYPT_MAX_PASSWORD_BYTES)
        return "";

    error = catch(result = crypt(password, BCRYPT_PREFIX
        + generate_bcrypt_salt()));
    if (error || !stringp(result) || strsrch(result, BCRYPT_PREFIX) != 0)
        return "";

    return result;
}

int verify_password(string password, string password_hash)
{
    string result;
    mixed error;

    if (!stringp(password) || !stringp(password_hash)
        || password == "" || password_hash == ""
        || strsrch(password_hash, BCRYPT_PREFIX) != 0)
        return 0;

    error = catch(result = crypt(password, password_hash));
    return !error && stringp(result) && result == password_hash;
}

/* 本驱动 strlen 按字符计数、下标返回码位：bcrypt 的 72 字节上限必须自己按
 * UTF-8 编码长度计算，否则 24 个汉字（72 字节）与 25 个汉字（75 字节）无法区分。 */
int utf8_byte_length(string text)
{
    int i;
    int len;
    int bytes;
    int codepoint;

    if (!stringp(text))
        return 0;

    len = strlen(text);
    bytes = 0;
    for (i = 0; i < len; i++) {
        codepoint = text[i];
        if (!intp(codepoint))
            bytes += 1;
        else if (codepoint < 0x80)
            bytes += 1;
        else if (codepoint < 0x800)
            bytes += 2;
        else if (codepoint < 0x10000)
            bytes += 3;
        else
            bytes += 4;
    }

    return bytes;
}

int query_utf8_byte_length(string text)
{
    return utf8_byte_length(text);
}

/* 密码策略错误："" 表示可哈希；否则给出可直接回报的原因。 */
string query_password_policy_error(string password)
{
    if (!stringp(password) || password == "")
        return "empty_password";
    if (utf8_byte_length(password) > BCRYPT_MAX_PASSWORD_BYTES)
        return "password_exceeds_bcrypt_limit";
    return "";
}

int query_password_max_bytes()
{
    return BCRYPT_MAX_PASSWORD_BYTES;
}

string query_hash_kind(string password_hash)
{
    if (!stringp(password_hash) || password_hash == "")
        return "none";
    if (strsrch(password_hash, BCRYPT_PREFIX) == 0)
        return "bcrypt";
    return "invalid";
}
