package com.xiakexing.client.core.network

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * Process-lifetime owner for the one connection graph.
 *
 * The Application creates exactly one instance. Activities and Services only
 * acquire short-lived attachment tokens; neither host may create a repository,
 * a credential store, or a reconnect scope.
 */
class ProcessConnectionOwner(
    val scope: CoroutineScope,
    val repository: GameConnectionRepository,
    val publicEndpoint: PublicEndpoint,
    private val headlessBootstrapDelayMs: Long = DEFAULT_HEADLESS_BOOTSTRAP_DELAY_MS,
) {
    data class OwnerState(
        val uiAttachmentCount: Int = 0,
        val serviceAttachmentCount: Int = 0,
    ) {
        val uiAttached: Boolean get() = uiAttachmentCount > 0
    }

    private val lock = Any()
    private var uiAttachmentCount = 0
    private var serviceAttachmentCount = 0
    private var headlessBootstrapJob: Job? = null
    private val _state = MutableStateFlow(OwnerState())
    val state: StateFlow<OwnerState> = _state.asStateFlow()

    init {
        repository.startCollecting()
    }

    fun attachUi(): ConnectionAttachment {
        synchronized(lock) {
            uiAttachmentCount += 1
            publishStateLocked()
        }
        repository.onUiSubscribed()
        return Attachment { detachUi() }
    }

    fun attachService(): ConnectionAttachment {
        synchronized(lock) {
            serviceAttachmentCount += 1
            publishStateLocked()
            if (serviceAttachmentCount == 1) scheduleHeadlessBootstrapLocked()
        }
        return Attachment { detachService() }
    }

    fun attachedUiCount(): Int = synchronized(lock) { uiAttachmentCount }

    fun attachedServiceCount(): Int = synchronized(lock) { serviceAttachmentCount }

    private fun detachUi() {
        val shouldDetach =
            synchronized(lock) {
                if (uiAttachmentCount == 0) {
                    false
                } else {
                    uiAttachmentCount -= 1
                    publishStateLocked()
                    true
                }
            }
        if (shouldDetach) repository.onUiUnsubscribed()
    }

    private fun detachService() {
        synchronized(lock) {
            if (serviceAttachmentCount == 0) return
            serviceAttachmentCount -= 1
            publishStateLocked()
            if (serviceAttachmentCount == 0) {
                headlessBootstrapJob?.cancel()
                headlessBootstrapJob = null
            }
        }
    }

    private fun publishStateLocked() {
        _state.value = OwnerState(uiAttachmentCount, serviceAttachmentCount)
    }

    fun closeForTest() {
        synchronized(lock) {
            headlessBootstrapJob?.cancel()
            headlessBootstrapJob = null
        }
        repository.shutdownForTest()
    }

    private fun scheduleHeadlessBootstrapLocked() {
        headlessBootstrapJob?.cancel()
        headlessBootstrapJob =
            scope.launch {
                delay(headlessBootstrapDelayMs)
                val shouldBootstrap =
                    synchronized(lock) {
                        serviceAttachmentCount > 0 && uiAttachmentCount == 0
                    }
                if (shouldBootstrap && !repository.hasActiveConnection()) {
                    repository.connectResume(publicEndpoint.toGatewayEndpoint())
                }
            }
    }

    private class Attachment(
        private val onClose: () -> Unit,
    ) : ConnectionAttachment {
        private var closed = false

        override fun close() {
            if (closed) return
            closed = true
            onClose()
        }
    }

    private companion object {
        const val DEFAULT_HEADLESS_BOOTSTRAP_DELAY_MS = 2_000L
    }
}

interface ConnectionAttachment : AutoCloseable {
    override fun close()
}

interface ProcessConnectionOwnerProvider {
    val processConnectionOwner: ProcessConnectionOwner
}

/** A non-persistent store used only by debug/test fallback construction. */
object EmptyGameCredentialStore : GameCredentialStore {
    override suspend fun load(): GameCredentials = GameCredentials("", "")

    override suspend fun save(
        username: String,
        password: String,
    ) = Unit
}
