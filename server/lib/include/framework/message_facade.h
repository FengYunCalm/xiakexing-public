/**
 * @file message_facade.h
 * @brief 消息外观接口
 *
 * 这些声明描述的是统一消息入口的公共契约。
 * 调用方传入的字符串应视为最终展示文本或已完成上层协议包装的文本，
 * 本接口不转移对象或字符串所有权，也不保证离线对象一定会收到消息。

 */

#pragma strict_types

#ifndef MESSAGE_FACADE_H
#define MESSAGE_FACADE_H

/**
 * @brief 向对象发送消息
 * @param ob 目标对象
 * @param msg 消息内容
 * @note 前置条件是 ob 为可接收消息的有效对象；失败处理由底层消息实现决定。
 */
varargs void msg_send(object ob, string msg, mapping response_context);

/**
 * @brief 只发送旧文本，不自动补发 MSGE
 * @note 仅用于调用方已经手动发出专属结构/事件包的路径，避免通用 main 事件重复。
 */
void msg_send_legacy(object ob, string msg);

/**
 * @brief 发送玩家可见事件
 * @param ob 目标对象
 * @param msg 消息内容
 * @param options MSGE envelope 选项
 * @return 1=已按客户端类型发送；0=目标或文本无效
 */
int msg_event(object ob, string msg, mapping options);

/**
 * @brief 发送玩家可见错误消息并同步发出 MSGE error
 */
varargs int msg_error(object ob, string msg, string command, string reason,
    string request, int code, mapping response_context);

/**
 * @brief 发送系统反馈并同步发出 MSGE system
 */
varargs int msg_system(object ob, string msg, string kind,
    mapping response_context);

/**
 * @brief 发送弹窗/面板反馈并同步发出 MSGE dialog
 */
varargs int msg_dialog(object ob, string title, string msg, string dialog_id,
    string kind, mapping response_context);

/**
 * @brief 发送战斗叙事反馈并同步发出 MSGE combat
 */
varargs int msg_combat(object ob, string msg, string kind,
    mapping response_context);

/**
 * @brief 广播消息给所有玩家
 * @param msg 消息内容
 * @note 面向当前在线玩家集合，调用方不应假定广播顺序或原子可见性。
 */
void msg_broadcast(string msg);

/**
 * @brief 发送消息给房间内的所有对象
 * @param room 房间对象
 * @param msg 消息内容
 * @param exclude 排除的对象(可选)
 * @note room 可为房间对象或实现接受的房间标识；exclude 语义由实现解释，可为单对象或对象数组。
 */
void msg_to_room(mixed room, string msg, mixed exclude);

/**
 * @brief 只向房间发送旧文本，不自动补发 MSGE
 * @note 仅用于调用方已经手动发出专属结构/事件包的房间广播路径。
 */
void msg_to_room_raw(mixed room, string msg, mixed exclude);

#endif /* MESSAGE_FACADE_H */
