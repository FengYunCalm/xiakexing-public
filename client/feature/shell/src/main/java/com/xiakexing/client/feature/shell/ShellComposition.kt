package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.network.GameConnectionRepository
import com.xiakexing.client.core.network.GameCredentialStore
import com.xiakexing.client.core.protocol.XkStreamDecoder
import com.xiakexing.client.feature.shell.update.ClientUpdateManager
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers

/**
 * shell 组合根——coordinator 构造与 Sink 网集中在此，
 * 产物为不可变 [ShellGraph]。
 *
 * 旧 ShellCompositionBindings 注册表（lateinit + buildAll 顺序 +
 * uninitializedProperties 自检）已删除：域 factory（*Bindings）只接收
 * [ShellSharedDeps] 与显式的最小 coordinator 依赖面；跨域环通过 [LazyRef]
 * 显式断环，本函数在依赖方向允许时及时 bind（保持原 buildAll 顺序语义，
 * Sink 闭包调用时求值，未 bind 即取用 fail loud）。
 * 登录语义与未接入回退保留在 ShellPacketHandler；BACH 走 ingress 独立路径。
 *
 * [ShellSharedDeps] 是域 factory 的共享依赖包（非注册表——不含任何 coordinator）。
 */
internal class ShellSharedDeps(
    val scope: CoroutineScope,
    val connectionRepository: GameConnectionRepository,
    val connectionSession: ShellConnectionSessionState,
    val combatShortcutStore: ShellCombatShortcutStore,
    val credentialsStore: GameCredentialStore,
    val updateManager: ClientUpdateManager,
    val automaticUpdateChecksEnabled: Boolean,
    val streamDecoder: XkStreamDecoder,
    val protocolDecodeDispatcher: CoroutineDispatcher,
    val stateBridge: ShellCompositionStateBridge,
    val vmBridge: ShellCompositionVmBridge,
)

/** 唯一构造入口：按显式依赖顺序构造全部 owner 并组装不可变 graph。 */
internal fun buildShellGraph(
    scope: CoroutineScope,
    connectionRepository: GameConnectionRepository,
    combatShortcutStore: ShellCombatShortcutStore,
    credentialsStore: GameCredentialStore,
    updateManager: ClientUpdateManager,
    automaticUpdateChecksEnabled: Boolean,
    streamDecoder: XkStreamDecoder,
    protocolDecodeDispatcher: CoroutineDispatcher = Dispatchers.Default,
    stateBridge: ShellCompositionStateBridge,
    vmBridge: ShellCompositionVmBridge,
): ShellGraph {
    val deps =
        ShellSharedDeps(
            scope = scope,
            connectionRepository = connectionRepository,
            connectionSession = ShellConnectionSessionState(connectionRepository),
            combatShortcutStore = combatShortcutStore,
            credentialsStore = credentialsStore,
            updateManager = updateManager,
            automaticUpdateChecksEnabled = automaticUpdateChecksEnabled,
            streamDecoder = streamDecoder,
            protocolDecodeDispatcher = protocolDecodeDispatcher,
            stateBridge = stateBridge,
            vmBridge = vmBridge,
        )

    // 跨域环依赖显式断点：Sink 闭包调用时求值；bind 时机跟随原 buildAll
    // 顺序——owner 构造完成即可 bind，未 bind 即取用会 fail loud。
    val interactionSessionRef = LazyRef<InteractionSessionCoordinator>()
    val messageRef = LazyRef<MessageCoordinator>()
    val inventoryTradeRef = LazyRef<InventoryTradeCoordinator>()
    val surfaceRef = LazyRef<SurfaceCoordinator>()
    val questRef = LazyRef<QuestCoordinator>()
    val roomProjectionRef = LazyRef<RoomProjectionCoordinator>()
    val loginRef = LazyRef<LoginCoordinator>()
    val skillRef = LazyRef<SkillCoordinator>()
    val panelRef = LazyRef<PanelCoordinator>()
    val packetHandlerRef = LazyRef<ShellPacketHandler>()

    // Core 域（含 leaf：projectionRuntime/clientUpdate/persistence/capture）。
    val core =
        CoreBindings(
            deps,
            messageRef,
            inventoryTradeRef,
            surfaceRef,
            questRef,
            roomProjectionRef,
            packetHandlerRef,
            interactionSessionRef,
        ).build()
    // interactionSession 是 Message 域 InventoryTradeCoordinator 的构造参数
    // （立即求值），必须在 Message 前 bind。
    interactionSessionRef.bind(core.interactionSessionCoordinator)

    // Login 域。
    val login = LoginBindings(deps, messageRef, core.persistenceCoordinator).build()
    loginRef.bind(login.loginCoordinator)

    // Room 域（quest/roomProjection 域内互引环在域内断）。
    val room =
        RoomBindings(
            deps,
            interactionSessionRef,
            messageRef,
            loginRef,
            panelRef,
        ).build()
    questRef.bind(room.questCoordinator)
    roomProjectionRef.bind(room.roomProjectionCoordinator)

    // Command 域。
    val command =
        CommandBindings(
            deps,
            interactionSessionRef,
            roomProjectionRef,
            messageRef,
        ).build()

    // Surface 域（surface/quantityFlow 域内互引环在域内断）。
    val surface =
        SurfaceBindings(
            deps,
            interactionSessionRef,
            panelRef,
            skillRef,
            inventoryTradeRef,
            messageRef,
            command.commandCoordinator,
            room.questCoordinator,
            core.clientUpdateCoordinator,
        ).build()
    surfaceRef.bind(surface.surfaceCoordinator)

    // Combat 域（connectionPresentation/combat/skill，域内 combat→skill 环在域内断）。
    val combat =
        CombatBindings(
            deps,
            messageRef,
            roomProjectionRef,
            interactionSessionRef,
            questRef,
            loginRef,
            surfaceRef,
            inventoryTradeRef,
            core.protocolIngressCoordinator,
            core.clientUpdateCoordinator,
            core.persistenceCoordinator,
        ).build()
    skillRef.bind(combat.skillCoordinator)

    // Message 域（inventoryTrade → message 同域前向）。
    val message =
        MessageBindings(
            deps,
            interactionSessionRef,
            loginRef,
            panelRef,
            roomProjectionRef,
            core.interactionCaptureOrchestrator,
        ).build()
    messageRef.bind(message.messagePacketCoordinator)
    inventoryTradeRef.bind(message.inventoryTradeCoordinator)

    // Sheet 域。
    val sheet = SheetBindings(deps, roomProjectionRef, interactionSessionRef, surfaceRef).build()
    panelRef.bind(sheet.panelCoordinator)

    // 路由表：全部 owner 就绪后接收不可变 owner 集合构造；登录语义留在
    // ShellPacketHandler，BACH 走 ingress 独立路径。
    val packetOwnerRoute =
        buildDefaultPacketOwnerRoute(
            room = room.roomProjectionCoordinator,
            quest = room.questCoordinator,
            skill = combat.skillCoordinator,
            combat = combat.combatPacketCoordinator,
            message = message.messagePacketCoordinator,
            interactionSession = core.interactionSessionCoordinator,
            inventoryTrade = message.inventoryTradeCoordinator,
            projectionRuntime = core.projectionRuntimeCoordinator,
            login = login.loginCoordinator,
        )
    val packetHandler = ShellPacketHandler(packetOwnerRoute, deps.vmBridge)
    packetHandlerRef.bind(packetHandler)

    return ShellGraph(
        connectionSession = deps.connectionSession,
        interactionCaptureState = core.interactionCaptureState,
        interactionCaptureOrchestrator = core.interactionCaptureOrchestrator,
        interactionSessionCoordinator = core.interactionSessionCoordinator,
        protocolIngressCoordinator = core.protocolIngressCoordinator,
        projectionRuntimeCoordinator = core.projectionRuntimeCoordinator,
        clientUpdateCoordinator = core.clientUpdateCoordinator,
        persistenceCoordinator = core.persistenceCoordinator,
        connectionPresentationCoordinator = combat.connectionPresentationCoordinator,
        loginCoordinator = login.loginCoordinator,
        surfaceCoordinator = surface.surfaceCoordinator,
        roomProjectionCoordinator = room.roomProjectionCoordinator,
        commandCoordinator = command.commandCoordinator,
        quantityFlowCoordinator = surface.quantityFlowCoordinator,
        combatPacketCoordinator = combat.combatPacketCoordinator,
        skillCoordinator = combat.skillCoordinator,
        messagePacketCoordinator = message.messagePacketCoordinator,
        questCoordinator = room.questCoordinator,
        inventoryTradeCoordinator = message.inventoryTradeCoordinator,
        panelCoordinator = sheet.panelCoordinator,
        packetOwnerRoute = packetOwnerRoute,
        packetHandler = packetHandler,
    )
}
