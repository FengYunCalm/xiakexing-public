package com.xiakexing.client.feature.shell

import android.app.Application
import android.content.pm.ApplicationInfo
import com.xiakexing.client.core.network.PublicEndpoint
import com.xiakexing.client.feature.shell.update.ClientUpdateManager

internal fun createClientUpdateManager(
    application: Application,
    publicEndpoint: PublicEndpoint,
): ClientUpdateManager =
    ClientUpdateManager(
        application,
        publicEndpoint = publicEndpoint,
        enforceBuildIdentity = (application.applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE) == 0,
    )

internal fun updateShellState(
    current: ShellUiState,
    transform: ShellUiState.() -> ShellUiState,
): ShellUiState =
    reconcileSheetNavigation(
        current,
        current.transform().retainRequiredClientUpdate(current),
    )
