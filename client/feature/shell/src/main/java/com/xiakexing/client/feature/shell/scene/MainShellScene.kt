package com.xiakexing.client.feature.shell.scene

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.runtime.State
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.zIndex
import com.xiakexing.client.core.protocol.payload.PlayerStatusSnapshot
import com.xiakexing.client.feature.shell.MainShellSceneState
import com.xiakexing.client.feature.shell.R
import com.xiakexing.client.feature.shell.ShellIntent
import com.xiakexing.client.feature.shell.component.ReconnectOverlay
import com.xiakexing.client.feature.shell.component.SketchShellLayout
import com.xiakexing.client.feature.shell.ui.ShellChrome
import com.xiakexing.client.feature.shell.ui.ShellPalette

/** 重连覆盖层层序：与 SketchShellLayout 同父兄弟，盖住其整棵子树（sketch 根默认 z0）。 */
private const val RECONNECT_OVERLAY_Z_INDEX = 40f

@Composable
internal fun MainShellScene(
    state: MainShellSceneState,
    statusSnapshotState: State<PlayerStatusSnapshot?>,
    chatInputState: State<String>,
    onIntent: (ShellIntent) -> Unit,
) {
    Box(modifier = Modifier.fillMaxSize()) {
        MainShellFrameBackground()
        SketchShellLayout(
            state = state,
            statusSnapshotState = statusSnapshotState,
            chatInputState = chatInputState,
            onIntent = onIntent,
            modifier =
                Modifier
                    .fillMaxSize()
                    .padding(
                        start = ShellChrome.FramePadding,
                        end = ShellChrome.FramePadding,
                    ),
        )
        if (state.reconnectOverlay) {
            ReconnectOverlay(
                timedOut = state.reconnectTimedOut,
                onRetry = { onIntent(ShellIntent.ReconnectRetryClicked) },
                onBackToLogin = { onIntent(ShellIntent.ReconnectBackToLoginClicked) },
                modifier =
                    Modifier
                        .fillMaxSize()
                        .zIndex(RECONNECT_OVERLAY_Z_INDEX),
            )
        }
    }
}

@Composable
private fun MainShellFrameBackground() {
    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .background(ShellPalette.MainSurface),
    ) {
        Image(
            painter = painterResource(R.drawable.shell_bg_main_dark_ink),
            contentDescription = null,
            contentScale = ContentScale.Crop,
            modifier = Modifier.fillMaxSize(),
        )
        Box(
            modifier =
                Modifier
                    .fillMaxSize()
                    .background(
                        ShellPalette.MainSurface.copy(
                            alpha = ShellChrome.BackgroundImageScrimAlpha,
                        ),
                    ),
        )
    }
}
