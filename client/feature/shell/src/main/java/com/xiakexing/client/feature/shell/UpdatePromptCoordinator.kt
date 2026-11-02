package com.xiakexing.client.feature.shell

import com.xiakexing.client.feature.shell.update.ClientUpdateDownloadProgress
import com.xiakexing.client.feature.shell.update.ClientUpdateManager
import com.xiakexing.client.feature.shell.update.ClientUpdateManifest
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.delay
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.launch

internal class UpdatePromptCoordinator(
    private val scope: CoroutineScope,
    private val updateManager: ClientUpdateManager,
    private val automaticUpdateChecksEnabled: Boolean,
    private val sink: Sink,
) {
    interface Sink {
        fun updateState(transform: ShellUiState.() -> ShellUiState)

        fun updateSceneState(): ShellScene

        fun disconnectForRequiredUpdate()
    }

    private var updateCheckJob: Job? = null
    private var updateDownloadJob: Job? = null
    private var latestUpdateManifest: ClientUpdateManifest? = null
    private var latestUpdateRequired: Boolean = false
    private var automaticUpdateCheckRequested: Boolean = false

    // 方案 R-21：世界加载期间到达的更新包挂起，待离开加载场景再呈现（不打断加载动画）
    private var pendingUpdateManifest: ClientUpdateManifest? = null
    private var pendingUpdateRequired: Boolean = false
    private var pendingUpdateWatchJob: Job? = null

    @Volatile
    private var updateCheckRequestId: Long = 0L

    @Volatile
    private var updateDownloadRequestId: Long = 0L

    fun installedVersionName(): String = updateManager.installedVersion().versionName

    fun maybeStartAutomaticUpdateCheck() {
        if (!automaticUpdateChecksEnabled || automaticUpdateCheckRequested) {
            return
        }
        automaticUpdateCheckRequested = true
        startUpdateCheck(silent = true, reportFailureWhenSilent = true)
    }

    fun checkForClientUpdate(silent: Boolean) {
        startUpdateCheck(silent = silent, reportFailureWhenSilent = false)
    }

    private fun startUpdateCheck(
        silent: Boolean,
        reportFailureWhenSilent: Boolean,
    ) {
        updateCheckJob?.cancel()
        val requestId = updateCheckRequestId + 1L
        updateCheckRequestId = requestId
        pendingUpdateWatchJob?.cancel()
        pendingUpdateWatchJob = null
        pendingUpdateManifest = null
        pendingUpdateRequired = false
        if (!silent) {
            sink.updateState { showUpdateCheckingState() }
        }
        updateCheckJob =
            scope.launch {
                runUpdateCheck(requestId, silent, reportFailureWhenSilent)
            }
    }

    private suspend fun runUpdateCheck(
        requestId: Long,
        silent: Boolean,
        reportFailureWhenSilent: Boolean,
    ) {
        try {
            val manifest = updateManager.fetchManifest()
            val installed = updateManager.installedVersion()
            currentCoroutineContext().ensureActive()
            if (requestId != updateCheckRequestId) return
            val required = manifest.isRequiredFor(installed)
            if (updateManager.isApplicableTrusted(manifest, installed)) {
                latestUpdateManifest = manifest
                latestUpdateRequired = required
                if (required) {
                    sink.updateState { copy(clientUpdateRequired = true) }
                    sink.disconnectForRequiredUpdate()
                }
                presentUpdateAvailable(manifest, required)
            } else if (!silent) {
                sink.updateState { showNoUpdateState(installed.versionName) }
            }
        } catch (error: CancellationException) {
            throw error
        } catch (error: Throwable) {
            if (requestId != updateCheckRequestId) return
            if (!silent || reportFailureWhenSilent) {
                sink.updateState {
                    copy(
                        dialog =
                            buildUpdateErrorPopup(
                                error.message ?: "更新检查失败",
                                required = clientUpdateRequired,
                            ),
                    )
                }
            }
        }
    }

    private fun presentUpdateAvailable(
        manifest: ClientUpdateManifest,
        required: Boolean,
    ) {
        if (sink.updateSceneState() == ShellScene.WORLD_LOADING) {
            pendingUpdateManifest = manifest
            pendingUpdateRequired = required
            schedulePendingUpdatePresentation()
            return
        }
        sink.updateState { showUpdateAvailableState(manifest, required = required) }
    }

    /** 世界加载场景离开后呈现挂起的更新 popup（轮询等待，不打断加载动画）。 */
    private fun schedulePendingUpdatePresentation() {
        pendingUpdateWatchJob?.cancel()
        pendingUpdateWatchJob =
            scope.launch {
                while (pendingUpdateManifest != null) {
                    delay(400L)
                    val manifest = pendingUpdateManifest ?: break
                    if (sink.updateSceneState() != ShellScene.WORLD_LOADING) {
                        val required = pendingUpdateRequired
                        pendingUpdateManifest = null
                        pendingUpdateRequired = false
                        pendingUpdateWatchJob = null
                        sink.updateState { showUpdateAvailableState(manifest, required = required) }
                        break
                    }
                }
            }
    }

    fun downloadClientUpdate() {
        val manifest = latestUpdateManifest ?: return
        val requestId = updateDownloadRequestId + 1L
        updateDownloadRequestId = requestId
        val required = latestUpdateRequired
        updateDownloadJob?.cancel()
        sink.updateState {
            showUpdateDownloadingState(
                ClientUpdateDownloadProgress(downloadedBytes = 0L, totalBytes = manifest.size),
            )
        }
        updateDownloadJob =
            scope.launch {
                try {
                    val file =
                        updateManager.downloadAndVerify(manifest) { progress ->
                            if (updateDownloadRequestId == requestId) {
                                sink.updateState { showUpdateDownloadingState(progress) }
                            }
                        }
                    currentCoroutineContext().ensureActive()
                    if (updateDownloadRequestId != requestId || latestUpdateManifest != manifest) return@launch
                    val installStarted = updateManager.openInstall(file)
                    if (updateDownloadRequestId != requestId) return@launch
                    if (!installStarted) {
                        sink.updateState {
                            copy(dialog = buildUpdateInstallPermissionPopup(required = required))
                        }
                    }
                } catch (error: CancellationException) {
                    throw error
                } catch (error: Throwable) {
                    if (updateDownloadRequestId != requestId) return@launch
                    sink.updateState {
                        copy(
                            dialog =
                                buildUpdateErrorPopup(
                                    error.message ?: "更新下载失败",
                                    required = required,
                                ),
                        )
                    }
                }
            }
    }

    fun cancel() {
        updateCheckRequestId += 1L
        updateCheckJob?.cancel()
        updateCheckJob = null
        updateDownloadRequestId += 1L
        updateDownloadJob?.cancel()
        updateDownloadJob = null
        pendingUpdateWatchJob?.cancel()
        pendingUpdateWatchJob = null
        pendingUpdateManifest = null
    }
}
