package com.xiakexing.client.feature.shell

import android.app.Application
import android.content.pm.ApplicationInfo
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.xiakexing.client.core.network.EmptyGameCredentialStore
import com.xiakexing.client.core.network.GameConnectionRepository
import com.xiakexing.client.core.network.GameCredentialStore
import com.xiakexing.client.core.network.ProcessConnectionOwnerProvider
import com.xiakexing.client.core.network.PublicEndpoint
import com.xiakexing.client.core.network.PublicEndpointDefaults
import com.xiakexing.client.core.network.PublicEndpointProvider
import com.xiakexing.client.core.protocol.IncomingFrame
import com.xiakexing.client.core.protocol.XkStreamDecoder
import com.xiakexing.client.core.protocol.payload.PlayerStatusSnapshot
import com.xiakexing.client.feature.shell.update.ClientUpdateManager
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

private fun isDebugApplication(application: Application): Boolean =
    (application.applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE) != 0

private fun resolvePublicEndpoint(application: Application): PublicEndpoint {
    val provider = application as? PublicEndpointProvider
    if (provider != null) return provider.publicEndpoint
    check(isDebugApplication(application)) {
        "Application must provide a public endpoint outside debug/test builds"
    }
    return PublicEndpointDefaults.origin
}

private fun resolveCredentialsStore(application: Application): GameCredentialStore {
    val owner = (application as? ProcessConnectionOwnerProvider)?.processConnectionOwner
    if (owner != null) return owner.repository.credentialsStore
    check(isDebugApplication(application)) {
        "Application must provide the process connection owner outside debug/test builds"
    }
    return EmptyGameCredentialStore
}

private fun resolveConnectionRepository(
    application: Application,
    credentialsStore: GameCredentialStore,
): GameConnectionRepository {
    val owner = (application as? ProcessConnectionOwnerProvider)?.processConnectionOwner
    if (owner != null && credentialsStore === owner.repository.credentialsStore) {
        return owner.repository
    }
    check(isDebugApplication(application)) {
        "Production ShellViewModel must use the Application-owned repository"
    }
    return GameConnectionRepository(
        CoroutineScope(SupervisorJob() + Dispatchers.Default),
        credentialsStore,
    ).also { it.startCollecting() }
}

class ShellViewModel internal constructor(
    application: Application,
    internal val credentialsStore: GameCredentialStore = resolveCredentialsStore(application),
    internal val combatShortcutStore: ShellCombatShortcutStore =
        SharedPreferencesCombatShortcutStore(application),
    internal val updateManager: ClientUpdateManager =
        createClientUpdateManager(application, resolvePublicEndpoint(application)),
    internal val automaticUpdateChecksEnabled: Boolean =
        (application.applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE) == 0,
    internal val connectionRepository: GameConnectionRepository =
        resolveConnectionRepository(application, credentialsStore),
    internal val protocolDecodeDispatcher: CoroutineDispatcher = Dispatchers.Default,
    internal val publicEndpoint: PublicEndpoint = resolvePublicEndpoint(application),
) : AndroidViewModel(application),
    ShellCompositionStateBridge {
    internal val _uiState =
        MutableStateFlow(
            ShellUiState()
                .withPublicEndpoint(publicEndpoint)
                .copy(clientVersion = updateManager.installedVersion().versionName),
        )
    val uiState: StateFlow<ShellUiState> = _uiState.asStateFlow()

    // 主壳状态全部由 uiState 派生（无第二状态源/手写 equality）
    internal val derivedAppScene: StateFlow<ShellScene> =
        _uiState.map {
            it.appScene
        }.distinctUntilChanged().stateIn(
            viewModelScope,
            SharingStarted.Eagerly,
            _uiState.value.appScene,
        )
    internal val derivedEntryScene: StateFlow<ShellEntrySceneState> =
        _uiState.map(
            ShellUiState::toEntrySceneState,
        ).distinctUntilChanged().stateIn(
            viewModelScope,
            SharingStarted.Eagerly,
            _uiState.value.toEntrySceneState(),
        )
    internal val derivedMainScene: StateFlow<MainShellSceneState> =
        _uiState.map(
            ShellUiState::toMainShellSceneState,
        ).distinctUntilChanged().stateIn(
            viewModelScope,
            SharingStarted.Eagerly,
            _uiState.value.toMainShellSceneState(),
        )
    internal val derivedStatusSnapshot: StateFlow<PlayerStatusSnapshot?> =
        _uiState.map {
            it.statusSnapshot
        }.distinctUntilChanged().stateIn(
            viewModelScope,
            SharingStarted.Eagerly,
            _uiState.value.statusSnapshot,
        )
    internal val derivedDialog: StateFlow<ShellPopupState?> =
        _uiState.map {
            it.dialog
        }.distinctUntilChanged().stateIn(
            viewModelScope,
            SharingStarted.Eagerly,
            _uiState.value.dialog,
        )
    internal val derivedCommandPalette: StateFlow<CommandPaletteState?> =
        _uiState.map {
            it.commandPalette
        }.distinctUntilChanged().stateIn(
            viewModelScope,
            SharingStarted.Eagerly,
            _uiState.value.commandPalette,
        )
    internal val derivedCommandInput: StateFlow<String> =
        _uiState.map {
            it.commandInput
        }.distinctUntilChanged().stateIn(
            viewModelScope,
            SharingStarted.Eagerly,
            _uiState.value.commandInput,
        )
    internal val derivedChatInput: StateFlow<String> =
        _uiState.map {
            it.chatInput
        }.distinctUntilChanged().stateIn(
            viewModelScope,
            SharingStarted.Eagerly,
            _uiState.value.chatInput,
        )

    internal val streamDecoder = XkStreamDecoder()
    internal var routePreviewTimeoutJob: Job? = null
    internal var routePreviewTimeoutRequestId: String? = null
    private val vmBridgeImpl: ShellCompositionVmBridgeImpl by lazy {
        ShellCompositionVmBridgeImpl(
            updateState = ::updateState,
            currentUiState = ::currentUiState,
            streamDecoder = streamDecoder,
            facade = { requireShellFacade },
            resumeLoginFallbackAllowed = { requireShellFacade.resumeLoginFallbackAllowed() },
            onLoginPromptReadyFn = { handleLoginPromptReady() },
            onLoginBootstrapCompleteFn = { handleLoginBootstrapComplete() },
            scheduleRoutePreviewTimeoutFn = ::scheduleRoutePreviewTimeout,
            cancelRoutePreviewTimeoutFn = ::cancelRoutePreviewTimeout,
        )
    }

    // ShellFacade 在 init 中构造（vmBridge 先进组合根，再回填门面）；
    // 桥方法调用时求值，构造完成后才可达。
    internal var shellFacade: ShellFacade? = null
    internal val requireShellFacade: ShellFacade
        get() = shellFacade ?: error("ShellFacade 未初始化")

    internal suspend fun handleIncomingMessage(rawMessage: String) {
        val preparedMessage = requireShellFacade.decodeIncomingMessage(rawMessage)
        withStateBatch {
            requireShellFacade.applyIncomingMessage(preparedMessage.segments)
        }
    }

    private suspend fun handleIncomingFrames(
        frames: List<IncomingFrame>,
        connectionGeneration: Long,
    ) {
        val preparedMessage = requireShellFacade.decodeIncomingFrames(frames)
        withStateBatch {
            requireShellFacade.applyIncomingMessage(
                preparedMessage.segments,
                connectionGeneration,
            )
        }
    }

    init {
        val graph =
            buildShellGraph(
                viewModelScope,
                connectionRepository,
                combatShortcutStore,
                credentialsStore,
                updateManager,
                automaticUpdateChecksEnabled,
                streamDecoder,
                protocolDecodeDispatcher,
                stateBridge = this,
                vmBridge = vmBridgeImpl,
            )
        shellFacade = ShellFacade(graph, connectionRepository, ::updateState, ::currentUiState, vmBridgeImpl)
        viewModelScope.launch {
            requireShellFacade.loadStoredCredentials()
        }
        viewModelScope.launch {
            requireShellFacade.loadStoredCombatShortcuts()
        }
        viewModelScope.launch {
            connectionRepository.decodedFrameBatches.collect { batch ->
                if (batch.generation == connectionRepository.connectionLifecycle.value.generation) {
                    handleIncomingFrames(batch.frames, batch.generation)
                }
            }
        }
        // UI 订阅建立：headless 自举重连循环让位于壳侧重连覆盖层。
        connectionRepository.onUiSubscribed()
        // phase collector 先于 resync 建立——resync 同步发 Connecting
        // 事件，SharedFlow replay=0，collector 未就绪会丢早期相位事件。
        viewModelScope.launch {
            connectionRepository.phaseEvents.collect(
                requireShellFacade::handlePhaseEvent,
            )
        }

        // headless 期间 Service 已完成 bootstrap 时，attach 先读生命周期
        // 快照并请求受控重同步（resume 语义，保留 resume token，服务端按
        // token 重放 REAL/BACH/STAT/IVTY/QUES）；凭据不在此路径重发（登录链
        // 只由服务端提示驱动，resume 失败时服务端重新提示才走登录链）。
        // 守卫：仅当 UI 首建于 bootstrap 完成后触发一次；Activity 旋转重建时
        // 会话已是本 UI 订阅期间完成的 bootstrap，不再重复 resync（bootstrapCompleted
        // 已在首次请求后复位，天然自守卫）。
        val attachSnapshot = connectionRepository.connectionLifecycle.value
        if (attachSnapshot.bootstrapCompleted) {
            connectionRepository.requestAuthoritativeResync()
        }

        // 三层 drop 可观测化：计数快照随帧消费更新，collect 喂给诊断 coordinator
        // （插桩属主在 coordinator，VM 只喂值）。collect 挂起不产生调度事件，
        // 避免在测试虚拟时间下形成永不 idle 的轮询循环。
        viewModelScope.launch {
            connectionRepository.dropCounters.collect { counters ->
                requireShellFacade.observeFrameDrop(
                    portDropped = counters.portDropped,
                    portProtocolDropped = counters.portProtocolDropped,
                    repositoryDropped = counters.repositoryDropped,
                )
            }
        }
    }

    fun onIntent(intent: ShellIntent) {
        requireShellFacade.onIntent(intent)
    }

    internal fun clearForTestInternal() {
        onCleared()
    }

    override fun onCleared() {
        // 先取消本 VM 的 raw collector（super.onCleared），再评估
        // headless 重连——否则 subscriptionCount 仍为 1，评估被短路成死代码。
        super.onCleared()
        connectionRepository.onUiUnsubscribed()
        requireShellFacade.onShellViewModelCleared()
        // 连接所有权归进程级 owner，VM 清理不断连（登出才断）。
    }

    internal fun scheduleRoutePreviewTimeout(requestId: String) {
        routePreviewTimeoutJob?.cancel()
        routePreviewTimeoutRequestId = requestId
        routePreviewTimeoutJob =
            viewModelScope.launch {
                delay(ROUTE_PREVIEW_TIMEOUT_MILLIS)
                if (routePreviewTimeoutRequestId != requestId) {
                    return@launch
                }
                routePreviewTimeoutJob = null
                routePreviewTimeoutRequestId = null
                requireShellFacade.handleRoutePreviewTimeout(requestId)
            }
    }

    internal fun cancelRoutePreviewTimeout(requestId: String) {
        if (routePreviewTimeoutRequestId != requestId) {
            return
        }
        routePreviewTimeoutJob?.cancel()
        routePreviewTimeoutJob = null
        routePreviewTimeoutRequestId = null
    }

    override fun updateStateWithSessionEvent(
        event: ShellEvent,
        transform: ShellUiState.() -> ShellUiState,
    ) = requireShellFacade.updateStateWithSessionEvent(event, transform)

    override fun updateState(transform: ShellUiState.() -> ShellUiState) {
        if (stateBatchDepth > 0) {
            batchedUiState = updateShellState(batchedUiState ?: _uiState.value, transform)
            return
        }
        _uiState.update { current -> updateShellState(current, transform) }
    }

    override fun currentUiState(): ShellUiState = batchedUiState ?: _uiState.value

    internal inline fun withStateBatch(block: () -> Unit) {
        stateBatchDepth += 1
        if (stateBatchDepth == 1) {
            batchedUiState = _uiState.value
        }

        try {
            block()
        } finally {
            stateBatchDepth -= 1
            if (stateBatchDepth == 0) {
                val nextState = batchedUiState
                batchedUiState = null
                if (nextState != null && nextState != _uiState.value) {
                    _uiState.value = nextState
                }
            }
        }
    }

    internal var stateBatchDepth: Int = 0
    internal var batchedUiState: ShellUiState? = null
}
