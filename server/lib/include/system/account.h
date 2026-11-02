/**
 * @file    account.h
 * @brief   账号与建角共享约束常量



 *
 * @par Description:
 * 登录内核、玩家服务、以及账号持久化都需要同一套长度边界。
 * 这些限制如果分散在多个文件中，很容易出现“认证层允许、存档层拒绝”
 * 或“建角文案与真实校验不一致”的漂移问题，因此统一收口到这里。
 */

#pragma strict_types

#ifndef __SYSTEM_ACCOUNT_H__
#define __SYSTEM_ACCOUNT_H__

#define ACCOUNT_MIN_USERNAME_LENGTH  2
#define ACCOUNT_MAX_USERNAME_LENGTH  32
#define ACCOUNT_MIN_PASSWORD_LENGTH  4
#define ACCOUNT_MAX_PASSWORD_LENGTH  64

#define CHARACTER_MIN_NAME_LENGTH    2
#define CHARACTER_MAX_NAME_LENGTH    6

#endif /* __SYSTEM_ACCOUNT_H__ */
