package com.xiakexing.client.feature.shell

/**
 * shell 组合根产物——不可变 typed graph。
 *
 * 全部 owner 在 [buildShellGraph] 中按显式依赖构造一次，返回值不可变：
 * 不再有注册表、lateinit 属性、buildAll 顺序或 uninitializedProperties 自检。
 * 域 factory（*Bindings）只接收显式依赖（[ShellSharedDeps] + 最小 coordinator
 * 依赖面），从不接收整个 graph/registry。
 */
internal class LazyRef<T : Any> {
    // 构造期环依赖的显式断点：Sink 闭包在调用时求值，跨域环
    // （interactionSession ↔ message 等）在全部 owner 就绪后统一 [bind]；
    // 未 bind 即取用 fail loud，与旧 lateinit 未初始化即访问的崩溃语义一致。
    private var value: T? = null

    fun bind(v: T) {
        check(value == null) { "LazyRef 重复绑定: ${v::class.simpleName}" }
        value = v
    }

    fun get(): T = value ?: error("LazyRef 未绑定: $this")
}

/** 组合根产物：全部 coordinator owner + 登录语义 handler + 路由表。 */
internal class ShellGraph(
    val connectionSession: ShellConnectionSessionState,
    val interactionCaptureState: InteractionCaptureState,
    val interactionCaptureOrchestrator: ShellInteractionCaptureOrchestrator,
    val interactionSessionCoordinator: InteractionSessionCoordinator,
    val protocolIngressCoordinator: ProtocolIngressCoordinator,
    val projectionRuntimeCoordinator: ProjectionRuntimeCoordinator,
    val clientUpdateCoordinator: UpdatePromptCoordinator,
    val persistenceCoordinator: PersistenceCoordinator,
    val connectionPresentationCoordinator: ConnectionPresentationCoordinator,
    val loginCoordinator: LoginCoordinator,
    val surfaceCoordinator: SurfaceCoordinator,
    val roomProjectionCoordinator: RoomProjectionCoordinator,
    val commandCoordinator: CommandCoordinator,
    val quantityFlowCoordinator: QuantityCoordinator,
    val combatPacketCoordinator: CombatCoordinator,
    val skillCoordinator: SkillCoordinator,
    val messagePacketCoordinator: MessageCoordinator,
    val questCoordinator: QuestCoordinator,
    val inventoryTradeCoordinator: InventoryTradeCoordinator,
    val panelCoordinator: PanelCoordinator,
    val packetOwnerRoute: PacketOwnerRoute,
    val packetHandler: ShellPacketHandler,
)
