/**
 * @file    xk_protocol/seq.c
 * @brief   XK 协议核心 - 序列号/流时代与协议观测
 *
 * @par Description:
 * 含 {load_server_seq_watermark, sync_server_seq_watermark, persist_protocol_counters, query_next_server_seq, reserve_contiguous_server_seq_block, reserve_batch_server_seq_block, xk_reserve_room_message_event_sequence_block, query_target_stream_epoch, should_auto_bump_stream, xk_bump_stream_epoch}。
 *
 * 本模块是 seq 域协议片段，由 `xk_protocol_core.c` 继承；
 * 只有本簇声明的 nosave 状态归属本簇，跨簇调用走 public 原型。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/constants.h>
#include <system/errors.h>
#include <system/services.h>
#include <system/xk_protocol.h>
#include <framework/message_facade.h>

/* server_seq is owned by the protocol-sequence service. This protocol core
 * only requests durable ranges; WORLD_STATE_D does not carry protocol state. */
int query_target_stream_epoch(object ob, string stream);
int should_auto_bump_stream(string stream);

int persist_protocol_counters()
{
    mixed err;
    int persisted;

    err = catch(persisted = PROTOCOL_SEQUENCE_D->persist_protocol_counters());
    if (err) {
        debug_message(sprintf("[xk_protocol_core] seq persistence failed err=%O", err));
        return 0;
    }
    return persisted ? 1 : 0;
}

int query_next_server_seq()
{
    mixed err;
    int first_server_seq;

    err = catch(first_server_seq = PROTOCOL_SEQUENCE_D->query_next_server_seq());
    return err ? 0 : first_server_seq;
}

int reserve_contiguous_server_seq_block(int count)
{
    mixed err;
    int first_server_seq;

    if (count <= 0)
        return 0;
    err = catch(first_server_seq =
        PROTOCOL_SEQUENCE_D->reserve_server_seq_block(count));
    return err ? 0 : first_server_seq;
}

/*
 * 批包一次申请“外层 + 全部子帧”的连续段，避免逐帧持久化调用。
 * 返回首序号；child_count 越界或持久段不可得时返回 0（调用方整批拒绝）。
 */
int reserve_batch_server_seq_block(int child_count)
{
    if (child_count < 0 || child_count > MAX_INT - 2)
        return 0;
    return reserve_contiguous_server_seq_block(1 + child_count);
}
mapping xk_reserve_room_message_event_sequence_block(object *targets)
{
    int *message_epochs;
    object target;
    int count;
    int first_server_seq;
    int index;

    if (!arrayp(targets) || sizeof(targets) == 0
        || sizeof(targets) > MAX_INT / 2)
        return ([]);
    count = sizeof(targets);
    first_server_seq = reserve_contiguous_server_seq_block(count * 2);
    if (first_server_seq <= 0)
        return ([]);
    message_epochs = allocate(count);
    foreach (target in targets) {
        if (!objectp(target))
            return ([]);
        message_epochs[index] = query_target_stream_epoch(target, "message");
        index++;
    }
    return ([
        "first_server_seq": first_server_seq,
        "message_epochs": message_epochs,
    ]);
}
int query_target_stream_epoch(object ob, string stream)
{
    mixed epoch;
    mixed err;

    if (!objectp(ob) || stream == "")
        return 0;

    /*
     * 快速路径：非 auto-bump 流（message/system 等）从未被 bump 写入，
     * 恒返回 0；避免每个 BACH 子包/每次 reservation 填充对目标做对象调用。
     * bump 是唯一写路径且被 should_auto_bump_stream 守卫包住（见
     * attach_protocol_meta_with_batch_epoch），语义不变。
     */
    if (!should_auto_bump_stream(stream))
        return 0;

    /*
     * epoch 持久化到目标对象属性（与 room_snapshot_revision 同模式）：
     * 玩家对象重建（MUD 重启/断线重连）后从存档恢复，epoch 跨重启单调，
     * 客户端 latestEpoch 不再失效；不再使用 nosave 内存映射（只增不清理，
     * 且 file_name 克隆号跨 boot 无身份意义）。
     */
    if (function_exists("query", ob)) {
        epoch = 0;
        err = catch(epoch = ob->query("xk/stream_epoch/" + stream));
        if (err)
            return 0;
        return intp(epoch) && epoch > 0 ? epoch : 0;
    }

    return 0;
}
int should_auto_bump_stream(string stream)
{
    return stream == "room" || stream == "status" || stream == "inventory";
}
int xk_bump_stream_epoch(object ob, string stream)
{
    int epoch;
    mixed err;

    if (!objectp(ob) || stream == "")
        return 0;
    /* 与读路径对称：非 auto-bump 流（message/system）从不 bump，防止绕过
     * 调用点守卫写入死状态（读路径忽略，快速路径移除后突然生效造成跳变）。 */
    if (!should_auto_bump_stream(stream))
        return 0;

    epoch = query_target_stream_epoch(ob, stream) + 1;
    /*
     * 持久化到目标对象属性：重建后继续递增，客户端排序不失效。
     * 不变量：auto-bump 目标必须是可持久化玩家对象（room/status/inventory
     * 均为玩家目标，见 should_auto_bump_stream 调用点）；无 set 的目标
     * 只能返回内存递增值，跨重启单调契约被破坏，必须留痕防静默回归。
     */
    if (function_exists("set", ob)) {
        err = catch(ob->set("xk/stream_epoch/" + stream, epoch));
        if (err) {
            log_file("system/session", sprintf(
                "[%s] xk_bump_stream_epoch persist failed %s:%s err=%O\n",
                ctime(time()), file_name(ob), stream, err));
            /* L-43：发送身份只能来自成功提交。写入失败时不发布新 epoch，
             * 返回旧值（调用方按“未 bump”处理），避免出现未持久化却已
             * 下发、重启后回退的假 epoch。 */
            return epoch - 1 > 0 ? epoch - 1 : 0;
        }
    } else {
        log_file("system/session", sprintf(
            "[%s] xk_bump_stream_epoch non-persistable target %s:%s (epoch monotonic contract degraded)\n",
            ctime(time()), file_name(ob), stream));
        /* 非持久目标同上：不能发布无法提交的发送身份。 */
        return epoch - 1 > 0 ? epoch - 1 : 0;
    }
    return epoch;
}
