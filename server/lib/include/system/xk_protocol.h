/**
 * @file    xk_protocol.h
 * @brief   XK 协议契约头 - XK Protocol Contract



 *
 * @par Description:
 * 定义 XK 通信协议的类型码常量与版本契约。
 * 协议格式: \x1bXK + 4位类型码 + JSON数据。
 * 本头文件只声明稳定的类型码常量，不承诺具体 JSON 字段集合；
 * 载荷结构以对应发送入口和客户端约定为准。
 * 所有类型码均面向线协议，必须保持 4 位大写字母，调用方不得在业务层
 * 擅自重写既有语义或复用现有类型码承载不兼容 payload。
 *
 */

#pragma strict_types

#ifndef __SYSTEM_XK_PROTOCOL_H__
#define __SYSTEM_XK_PROTOCOL_H__

#define XK_HEADER       "\x1bXK"  /* 线协议固定前缀，不包含长度信息，发送方需自行保证后续 payload 可完整解析 */

#define XK_LOGN         "LOGN"  /* 登录提示 */
#define XK_CAPA         "CAPA"  /* 能力协商，payload 通常描述客户端/服务端支持的特性集合 */
#define XK_PING         "PING"  /* 心跳请求 */
#define XK_PONG         "PONG"  /* 心跳响应 */
#define XK_ACKN         "ACKN"  /* 确认消息，payload 应能让接收方关联到被确认的请求或事件 */
#define XK_REDY         "REDY"  /* 服务器就绪 */

#define XK_REGI         "REGI"  /* 注册流程 */

#define XK_TITL         "TITL"  /* 房间标题，payload 通常包含标题文本及可选区域信息 */
#define XK_EXIT         "EXIT"  /* 出口信息，payload 语义为方向到出口描述/状态的映射 */
#define XK_NPCS         "NPCS"  /* NPC列表，payload 语义为房间可见 NPC 快照数组 */
#define XK_ITEM         "ITEM"  /* 物品列表，payload 语义为房间可见物品快照数组 */

#define XK_STAT         "STAT"  /* 玩家状态，payload 语义为当前玩家状态快照而非增量补丁 */
#define XK_HPBR         "HPBR"  /* 血条 */
#define XK_MPBR         "MPBR"  /* 蓝条 */
#define XK_TEXT         "TEXT"  /* 显式文本消息，payload 应包含 text/channel/mode/done */
#define XK_MSGE         "MSGE"  /* 统一消息事件，payload 语义为带展示意图和可靠性分层的玩家可感知事件 */
#define XK_SYST         "SYST"  /* 系统消息 */

#define XK_CBST         "CBST"  /* 战斗开始 */
#define XK_CBDM         "CBDM"  /* 伤害 */
#define XK_CBED         "CBED"  /* 战斗结束 */

#define XK_SKLS         "SKLS"  /* 技能列表 */
#define XK_IVTY         "IVTY"  /* 背包 */

#define XK_CHAT         "CHAT"  /* 聊天消息 */
#define XK_RPRT         "RPRT"

#define XK_CMD          "CMDS"  /* 命令消息 (原command) */
#define XK_LGOT         "LGOT"  /* 登出消息 (原logout) */
#define XK_UPDT         "UPDT"  /* 状态更新 (原update) */

#define XK_ERRO         "ERRO"  /* 错误消息 */

#define XK_PROTOCOL_VERSION_MAJOR    2
#define XK_PROTOCOL_VERSION_MINOR    3
#define XK_PROTOCOL_VERSION_STRING   "2.3"
#define XK_REPORT_SCHEMA_VERSION     1
#define XK_REPORT_HEALTHCHECK        "healthcheck"
#define XK_REPORT_SERVICECHECK       "servicecheck"
#define XK_REPORT_STATUS             "status"
#define XK_REPORT_SESSION_RESUME     "session_resume"
#define XK_REPORT_COMBATCHECK        "combatcheck"
#define XK_REPORT_DATAAUDIT          "dataaudit"
#define XK_REPORT_DATACLEAN          "dataclean"

#define CHECKSUM_MAX_CHARS    256
#define CHECKSUM_MODULO       10000

#define XK_BTNS         "BTNS"  /* 动态按钮，payload 语义为可渲染按钮配置数组 */
#define XK_ACT1         "ACT1"  /* 快捷按钮组1，payload 语义为第一组动作槽完整覆盖 */
#define XK_ACT2         "ACT2"  /* 快捷按钮组2，payload 语义为第二组动作槽完整覆盖 */
#define XK_MAPS         "MAPS"  /* 地图网格 */
#define XK_ROUT         "ROUT"  /* 路线预览，payload 语义为当前位置到目标房间的方向序列 */
#define XK_DLOG         "DLOG"  /* 对话框 */
#define XK_DIALOG_SCHEMA_VERSION  1  /* DLOG 载荷结构版本；单包默认 final=1、part_index=1、part_total=1 */
#define XK_PRMT         "PRMT"  /* 命令提示符 */
#define XK_BACH         "BACH"  /* 批量消息，payload 应为按顺序消费的子消息数组 */

#define XK_CBRD         "CBRD"  /* 回合信息，payload 语义为单回合结算快照 */
#define XK_CBSK         "CBSK"  /* 技能使用，payload 语义为一次技能释放或结算事件 */
#define XK_CBRT         "CBRT"  /* 战斗运行态，payload 语义为 busy/status/cooldown 权威快照 */
#define XK_CBEN         "CBEN"  /* 敌人信息，payload 语义为当前战斗敌方列表或目标快照 */

#define XK_SKLI         "SKLI"  /* 技能详情，payload 语义为单技能完整展示数据 */
#define XK_SKCD         "SKCD"  /* 技能冷却，payload 语义为技能冷却状态快照或更新 */

#define XK_SHOP         "SHOP"  /* 商店 */
#define XK_TRADE        "TRDE"  /* 交易 */
#define XK_SOCIAL       "SOCL"  /* 社交 */
#define XK_BUFF         "CBBF"  /* 增益效果 */

#define XK_QUEST          "QUES"  /* 任务列表 */
#define XK_QUEST_UPDATE   "QUEU"  /* 任务更新 */
#define XK_REAL "REAL"  /* 玩家投影身份 */

#define XK_ERR_OK               0
#define XK_ERR_INVALID_PARAM    2
#define XK_ERR_NOT_FOUND        9
#define XK_ERR_LOGIN_FAILED     1001
#define XK_ERR_PERMISSION       1002
#define XK_ERR_NOT_IN_COMBAT    6000
#define XK_ERR_SKILL_NOT_FOUND  7000

#endif /* __SYSTEM_XK_PROTOCOL_H__ */
