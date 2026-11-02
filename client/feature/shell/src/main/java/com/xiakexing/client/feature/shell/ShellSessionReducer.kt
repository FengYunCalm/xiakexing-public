package com.xiakexing.client.feature.shell

enum class ShellPhase {
    IDLE,
    CONNECTING,
    WAITING_LOGIN_PROMPT,
    AUTHENTICATING,
    REGISTERING,
    WAITING_WORLD,
    INTERACTIVE,
    SUSPENDED,
    FAILED,
    DISCONNECTED,
}

enum class WorldPacket {
    TITLE,
    EXIT,
    NPCS,
    ITEMS,
    STAT,
}

data class ShellSessionState(
    val phase: ShellPhase = ShellPhase.IDLE,
    val detail: String = "等待建立连接",
    val registerStage: String = "",
    val registerPrompt: String = "",
    val worldPackets: Set<WorldPacket> = emptySet(),
)

sealed interface ShellEvent {
    data object Reset : ShellEvent

    data object ConnectRequested : ShellEvent

    data object ConnectionEstablished : ShellEvent

    data object LoginPromptReceived : ShellEvent

    data object LoginBootstrapCompleted : ShellEvent

    data class RegisterPromptReceived(val stage: String, val message: String) : ShellEvent

    data class WorldPacketReceived(val packet: WorldPacket) : ShellEvent

    data class Suspended(val reason: String) : ShellEvent

    data class ConnectionFailed(val message: String) : ShellEvent

    data class Disconnected(val reason: String) : ShellEvent

    /** resume 连接发起（保留既有 worldPackets/register 态，仅切回 CONNECTING）。 */
    data object ResumeRequested : ShellEvent

    /** 世界切换：清空已收集的世界包集合（房间/TITL/EXIT/NPCS/ITEM 重新计）。 */
    data object WorldPacketsCleared : ShellEvent

    /** 会话详情文案更新（诊断/同步失败提示等，不改相位）。 */
    data class DetailUpdated(val detail: String) : ShellEvent
}

object ShellSessionReducer {
    private val requiredWorldPackets =
        setOf(
            WorldPacket.TITLE,
            WorldPacket.EXIT,
            WorldPacket.NPCS,
            WorldPacket.ITEMS,
        )

    fun reduce(
        state: ShellSessionState,
        event: ShellEvent,
    ): ShellSessionState {
        return when (event) {
            ShellEvent.Reset -> ShellSessionState()
            ShellEvent.ConnectRequested ->
                ShellSessionState(
                    phase = ShellPhase.CONNECTING,
                    detail = "正在连接线路",
                )

            ShellEvent.ConnectionEstablished ->
                state.copy(
                    phase = ShellPhase.WAITING_LOGIN_PROMPT,
                    detail = "连接成功，等待登录",
                    registerStage = "",
                    registerPrompt = "",
                    worldPackets = emptySet(),
                )

            ShellEvent.LoginPromptReceived ->
                state.copy(
                    phase = ShellPhase.AUTHENTICATING,
                    detail = "正在验证账号",
                    registerStage = "",
                    registerPrompt = "",
                )

            ShellEvent.LoginBootstrapCompleted ->
                state.copy(
                    phase = ShellPhase.INTERACTIVE,
                    detail = "已进入江湖",
                    registerStage = "",
                    registerPrompt = "",
                )

            is ShellEvent.RegisterPromptReceived ->
                state.copy(
                    phase = ShellPhase.REGISTERING,
                    detail =
                        when (event.stage.trim().lowercase()) {
                            "name" -> "注册流程：输入角色名字"
                            "gender" -> "注册流程：选择角色性别"
                            else -> "注册流程进行中"
                        },
                    registerStage = event.stage,
                    registerPrompt = event.message,
                )

            is ShellEvent.WorldPacketReceived -> {
                val packets = state.worldPackets + event.packet
                val ready = packets.containsAll(requiredWorldPackets)
                state.copy(
                    phase = if (ready) ShellPhase.INTERACTIVE else ShellPhase.WAITING_WORLD,
                    detail =
                        if (ready) {
                            "已进入江湖"
                        } else {
                            "正在载入场景 ${packets.size}/${requiredWorldPackets.size}"
                        },
                    worldPackets = packets,
                )
            }

            is ShellEvent.Suspended ->
                state.copy(
                    phase = ShellPhase.SUSPENDED,
                    detail = event.reason,
                )

            is ShellEvent.ConnectionFailed ->
                state.copy(
                    phase = ShellPhase.FAILED,
                    detail = event.message,
                )

            is ShellEvent.Disconnected ->
                state.copy(
                    phase = ShellPhase.DISCONNECTED,
                    detail = event.reason,
                )

            ShellEvent.ResumeRequested ->
                state.copy(
                    phase = ShellPhase.CONNECTING,
                    detail = "正在恢复连接",
                )

            ShellEvent.WorldPacketsCleared ->
                state.copy(worldPackets = emptySet())

            is ShellEvent.DetailUpdated ->
                state.copy(detail = event.detail)
        }
    }
}
