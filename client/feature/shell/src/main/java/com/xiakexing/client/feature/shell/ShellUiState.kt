package com.xiakexing.client.feature.shell

import androidx.compose.runtime.Immutable
import com.xiakexing.client.core.network.PublicEndpoint
import com.xiakexing.client.core.protocol.payload.ActiveSkillSlotPacket
import com.xiakexing.client.core.protocol.payload.ExitSnapshot
import com.xiakexing.client.core.protocol.payload.InlineAction
import com.xiakexing.client.core.protocol.payload.InventoryItemSnapshot
import com.xiakexing.client.core.protocol.payload.InventorySummary
import com.xiakexing.client.core.protocol.payload.MapGridPacket
import com.xiakexing.client.core.protocol.payload.NamedEntity
import com.xiakexing.client.core.protocol.payload.PlayerStatusSnapshot
import com.xiakexing.client.core.protocol.payload.SkillInfoPacket
import com.xiakexing.client.core.protocol.payload.SkillSummaryPacket

private val defaultLocalEndpoint = PublicEndpoint(host = "127.0.0.1", httpsPort = 8080)

internal const val WRECK_HOLD_DUNGEON_TYPE = "wreck_hold"
internal const val WRECK_HOLD_DUNGEON_NAME = "破船底舱"
internal const val WRECK_HOLD_ENTER_COMMAND = "dungeon enter wreck_hold"

internal fun ShellUiState.withPublicEndpoint(endpoint: PublicEndpoint): ShellUiState {
    val updatedServers =
        servers.map { server ->
            server.copy(
                host = endpoint.host,
                port = endpoint.httpsPort,
                path = "/ws",
                secure = !endpoint.isLoopback,
            )
        }
    return copy(
        servers = updatedServers,
        host = endpoint.host,
        portText = endpoint.httpsPort.toString(),
        path = "/ws",
        secure = !endpoint.isLoopback,
    )
}

private val officialServer =
    ServerOption(
        id = "official",
        label = "官方线路",
        description = "推荐线路",
        host = defaultLocalEndpoint.host,
        port = defaultLocalEndpoint.httpsPort,
        path = "/ws",
        secure = !defaultLocalEndpoint.isLoopback,
    )

@Immutable
data class ShellUiState(
    val appScene: ShellScene = ShellScene.LOGIN,
    val servers: List<ServerOption> = listOf(officialServer),
    val selectedServerId: String = officialServer.id,
    val host: String = defaultLocalEndpoint.host,
    val portText: String = defaultLocalEndpoint.httpsPort.toString(),
    val path: String = "/ws",
    val secure: Boolean = true,
    val username: String = "",
    val password: String = "",
    val commandInput: String = "",
    val session: ShellSessionState = ShellSessionState(),
    val roomId: String = "",
    val roomTitle: String = "",
    val roomZone: String = "",
    val instanceKind: String = "",
    val dungeonType: String = "",
    val dungeonGate: String = "",
    val roomRefreshSerial: Long = 0L,
    val exits: List<ExitSnapshot> = emptyList(),
    val specialExits: List<ExitSnapshot> =
        exits.filter {
            it.direction.lowercase() in specialExitDirections
        },
    val gridExits: List<ExitSnapshot> = exits.filter { it.direction.lowercase() in gridExitDirections },
    val npcs: List<NamedEntity> = emptyList(),
    val items: List<NamedEntity> = emptyList(),
    val inventoryItems: List<InventoryItemSnapshot> = emptyList(),
    val inventorySummary: InventorySummary? = null,
    val inventoryRefreshPending: Boolean = false,
    val statusSnapshot: PlayerStatusSnapshot? = null,
    val latestPrompt: String = "",
    val visibleTranscript: List<TranscriptLine> = emptyList(),
    val inlineActionChips: List<InlineAction> = emptyList(),
    val combatActive: Boolean = false,
    val combatOpponent: String = "",
    val combatOpponentVitals: CombatOpponentVitals = CombatOpponentVitals(),
    val combatOpponents: List<CombatOpponentState> = emptyList(),
    val combatStatusChips: List<CombatBuffChip> = emptyList(),
    val combatSelfBuffs: List<CombatBuffChip> = emptyList(),
    val combatSelfBusySeconds: Int = 0,
    val combatSelfBusyCapturedAtMillis: Long = 0L,
    val combatActionCooldowns: Map<String, CombatActionCooldown> = emptyMap(),
    val activeSkillSlots: Map<String, ActiveSkillSlotPacket> = emptyMap(),
    val topNoticeLines: List<String> = emptyList(),
    val floatingMessages: List<FloatingMessageState> = emptyList(),
    val chatMessages: List<ChatMessageItem> = emptyList(),
    val chatPanelOpen: Boolean = false,
    val chatUnreadCount: Int = 0,
    val unreadChatChannels: Set<ChatChannel> = emptySet(),
    val activeChatChannel: ChatChannel = ChatChannel.WORLD,
    val chatInput: String = "",
    /** 方案「减少动态效果」开关（会话级）：位移/缩放退化为 ≤100ms 淡变 */
    val reduceMotion: Boolean = false,
    /** 方案 R-20（S14）：主壳前台断线时的重连覆盖层；期间主壳保留但不可交互 */
    val reconnectOverlay: Boolean = false,
    /** 重连覆盖层超过 5s 未成功后，展示「重试 / 返回登录」 */
    val reconnectTimedOut: Boolean = false,
    val interactionObjects: List<InteractionObject> = buildInteractionObjects(npcs, items),
    val titleActions: List<TitleActionItem> = shellTitleActions,
    val transcriptScrollRequest: Int = 0,
    val loginErrorMessage: String = "",
    /** 方案 R-17：登录/注册页底部版本号（来自本机安装版本） */
    val clientVersion: String = "",
    /** 受信到的可信清单要求升级时锁定连接与普通操作，直到安装成功/重启。 */
    val clientUpdateRequired: Boolean = false,
    val dialog: ShellPopupState? = null,
    val sheet: ShellSheetState? = null,
    val sheetNavigation: SheetNavigationState = SheetNavigationState(),
    val commandPalette: CommandPaletteState? = null,
    val exitOverlay: ExitOverlayState? = null,
    val trackedQuest: TrackedQuestState = TrackedQuestState(),
    val questPanelSession: QuestPanelSessionState = QuestPanelSessionState(),
    val objectiveHub: ObjectiveHubState = ObjectiveHubState(),
    val projectionRuntime: ProjectionRuntimeState = ProjectionRuntimeState(),
    // room/quest/skill 最新协议事实唯一 owner（coordinator 不再持有重复 latest 缓存；
    // UI 派生模型 SkillPanelModel/CombatShortcutAction 为 internal 类型，保留 coordinator，见 §15.15）
    val latestMapGrid: MapGridPacket? = null,
    val latestSkillSummaries: List<SkillSummaryPacket> = emptyList(),
    val latestSkillInfo: SkillInfoPacket? = null,
) {
    val selectedServer: ServerOption
        get() = servers.firstOrNull { it.id == selectedServerId } ?: servers.first()

    val canConnect: Boolean
        get() =
            !clientUpdateRequired &&
                session.phase in setOf(ShellPhase.IDLE, ShellPhase.DISCONNECTED, ShellPhase.FAILED) &&
                host.isNotBlank() &&
                path.isNotBlank() &&
                portText.toIntOrNull() != null &&
                username.isNotBlank() &&
                password.isNotBlank()

    val canDisconnect: Boolean
        get() = session.phase !in setOf(ShellPhase.IDLE, ShellPhase.DISCONNECTED)

    val canSend: Boolean
        get() =
            !clientUpdateRequired &&
                commandInput.isNotBlank() && session.phase in
                setOf(
                    ShellPhase.REGISTERING,
                    ShellPhase.WAITING_WORLD,
                    ShellPhase.INTERACTIVE,
                )

    val phaseBadgeText: String
        get() =
            when (session.phase) {
                ShellPhase.IDLE -> "未连接"
                ShellPhase.CONNECTING -> "连接中"
                ShellPhase.WAITING_LOGIN_PROMPT -> "等待登录"
                ShellPhase.AUTHENTICATING -> "登录中"
                ShellPhase.REGISTERING -> "创建角色"
                ShellPhase.WAITING_WORLD -> "世界加载"
                ShellPhase.INTERACTIVE -> "已入江湖"
                ShellPhase.SUSPENDED -> "后台暂停"
                ShellPhase.FAILED -> "异常"
                ShellPhase.DISCONNECTED -> "已断开"
            }

    val selectedInteractionObjectId: String
        get() =
            sheet?.interactionTargetUiKey
                ?.takeIf { targetId -> targetId.isNotBlank() }
                ?: sheet?.interactionTargetId
                    ?.takeIf { targetId -> targetId.isNotBlank() }
                ?: ""

    val isDungeonEntryRoom: Boolean
        get() = instanceKind.isBlank() && dungeonGate.isNotBlank()

    val isInDungeonRoom: Boolean
        get() = instanceKind == "dungeon"

    val hasDungeonEntry: Boolean
        get() = isDungeonEntryRoom || isInDungeonRoom

    val activeDockButton: DockButtonType?
        get() =
            when {
                commandPalette != null -> DockButtonType.INPUT
                else -> sheet?.resolvedSheetKind().toDockButtonType()
            }

    val activeTitleAction: TitleActionType?
        get() =
            when {
                commandPalette != null -> TitleActionType.SYSTEM
                else -> sheet?.resolvedSheetKind().toTitleActionType()
            }

    val topNoticeTitle: String
        get() =
            when {
                appScene == ShellScene.MAIN_SHELL -> ""
                session.detail.isNotBlank() -> session.detail
                appScene == ShellScene.LOGIN -> loginErrorMessage.ifBlank { "输入姓名和密码" }
                appScene == ShellScene.REGISTER -> "按提示完成创建"
                appScene == ShellScene.WORLD_LOADING -> "正在进入江湖"
                else -> ""
            }

    val commandLabel: String
        get() =
            when (session.registerStage.trim().lowercase()) {
                "name" -> "角色名字"
                "gender" -> "角色性别"
                else -> "输入"
            }

    val commandPlaceholder: String
        get() =
            session.registerPrompt.ifBlank {
                latestPrompt.ifBlank {
                    if (session.phase in setOf(ShellPhase.WAITING_WORLD, ShellPhase.INTERACTIVE)) {
                        "请使用界面按钮操作"
                    } else {
                        "等待服务器提示..."
                    }
                }
            }

    val sendButtonText: String
        get() = if (session.phase == ShellPhase.REGISTERING) "提交注册" else "发送"

    val registerOptions: List<String>
        get() =
            when (session.registerStage.trim().lowercase()) {
                "gender" -> listOf("男", "女")
                else -> emptyList()
            }

    val showExitPanel: Boolean
        get() = session.worldPackets.contains(WorldPacket.EXIT)
}

/** Required update 是进程内锁定态，任何其他 surface 更新都不能把它静默清掉。 */
internal fun ShellUiState.retainRequiredClientUpdate(previous: ShellUiState): ShellUiState {
    if (!previous.clientUpdateRequired) {
        return this
    }
    val updateDialog =
        dialog?.takeIf { popup -> popup.meta.contains(CLIENT_UPDATE_POPUP_META) }
            ?: previous.dialog?.takeIf { popup -> popup.meta.contains(CLIENT_UPDATE_POPUP_META) }
    return copy(
        clientUpdateRequired = true,
        dialog = updateDialog,
    )
}

@Immutable
data class CombatOpponentVitals(
    val hp: Long? = null,
    val maxHp: Long? = null,
    val mp: Long? = null,
    val maxMp: Long? = null,
    val ep: Long? = null,
    val maxEp: Long? = null,
)

@Immutable
data class CombatOpponentState(
    val name: String,
    val vitals: CombatOpponentVitals = CombatOpponentVitals(),
)

@Immutable
data class CombatBuffChip(
    val id: String,
    val label: String,
    val category: String = "",
    val remainingSeconds: Int = 0,
    val remainingRounds: Int = 0,
    val stacks: Int = 1,
    val capturedAtMillis: Long = System.currentTimeMillis(),
)

internal fun CombatBuffChip.remainingSecondsAt(nowMillis: Long): Int {
    if (remainingSeconds <= 0) return 0
    val elapsedSeconds = ((nowMillis - capturedAtMillis).coerceAtLeast(0L) / 1000L).toInt()
    return (remainingSeconds - elapsedSeconds).coerceAtLeast(0)
}

private fun ShellSheetKind?.toDockButtonType(): DockButtonType? {
    return when (this) {
        ShellSheetKind.CHARACTER,
        ShellSheetKind.ATTRIBUTE,
        ShellSheetKind.INVENTORY,
        ShellSheetKind.EQUIPMENT,
        -> DockButtonType.CHARACTER
        ShellSheetKind.COMBAT -> DockButtonType.ACTION
        ShellSheetKind.WORLD -> DockButtonType.WORLD
        ShellSheetKind.QUEST -> DockButtonType.QUEST
        ShellSheetKind.MAP -> DockButtonType.MAP
        else -> null
    }
}

private fun ShellSheetKind?.toTitleActionType(): TitleActionType? {
    return when (this) {
        ShellSheetKind.MAIL -> TitleActionType.MESSAGE
        else -> null
    }
}
