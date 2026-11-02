/**
 * @file    trait.h
 * @brief   框架 trait 路径参考
 *
 * @par Description:
 * 只登记仓库内**真实存在**的框架路径宏。这些宏是路径参考，不是运行时接口声明：
 * 调用方仍要确认目标对象确实继承了对应能力。分层与命名规则以
 * `docs/systems/CODE_STANDARDS.md` 为准，本文件不重复承载设计说明。
 */

#ifndef __SYSTEM_TRAIT_H__
#define __SYSTEM_TRAIT_H__

#define F_DBASE                 "/kernel/framework/data"
#define F_NAME                  "/kernel/framework/name"
#define F_MOVE                  "/kernel/framework/move"
#define F_COMMAND               "/kernel/framework/command"
#define F_CLEANUP               "/kernel/framework/cleanup"
#define F_COMBAT                "/kernel/framework/attack"
#define F_SKILL                 "/kernel/framework/skill"

#endif /* __SYSTEM_TRAIT_H__ */
