package com.xiakexing.client.feature.shell

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.State
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Modifier
import com.xiakexing.client.core.protocol.payload.PlayerStatusSnapshot
import com.xiakexing.client.feature.shell.popup.ShellCommandPaletteHost
import com.xiakexing.client.feature.shell.popup.ShellPopupHost
import com.xiakexing.client.feature.shell.scene.LoginScene
import com.xiakexing.client.feature.shell.scene.MainShellScene
import com.xiakexing.client.feature.shell.scene.RegisterScene
import com.xiakexing.client.feature.shell.scene.WorldLoadingScene
import com.xiakexing.client.feature.shell.ui.LocalShellReduceMotion
import com.xiakexing.client.feature.shell.ui.ShellBackgroundColor

@Composable
fun ShellScreen(
    state: ShellUiState,
    onIntent: (ShellIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    val statusSnapshotState = rememberUpdatedState(state.statusSnapshot)
    val commandInputState = rememberUpdatedState(state.commandInput)
    val chatInputState = rememberUpdatedState(state.chatInput)
    ShellScreen(
        appScene = state.appScene,
        entrySceneState = state.toEntrySceneState(),
        mainShellSceneState = state.toMainShellSceneState(),
        statusSnapshotState = statusSnapshotState,
        popup = state.dialog,
        commandPalette = state.commandPalette,
        draftCommandState = commandInputState,
        chatInputState = chatInputState,
        onIntent = onIntent,
        modifier = modifier,
    )
}

@Composable
internal fun ShellScreen(
    appScene: ShellScene,
    entrySceneState: ShellEntrySceneState,
    mainShellSceneState: MainShellSceneState,
    statusSnapshotState: State<PlayerStatusSnapshot?>,
    popup: ShellPopupState?,
    commandPalette: CommandPaletteState?,
    draftCommand: String,
    chatInputState: State<String>,
    onIntent: (ShellIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    val draftCommandState = rememberUpdatedState(draftCommand)
    ShellScreen(
        appScene = appScene,
        entrySceneState = entrySceneState,
        mainShellSceneState = mainShellSceneState,
        statusSnapshotState = statusSnapshotState,
        popup = popup,
        commandPalette = commandPalette,
        draftCommandState = draftCommandState,
        chatInputState = chatInputState,
        onIntent = onIntent,
        modifier = modifier,
    )
}

@Composable
internal fun ShellScreen(
    appScene: ShellScene,
    entrySceneState: ShellEntrySceneState,
    mainShellSceneState: MainShellSceneState,
    statusSnapshotState: State<PlayerStatusSnapshot?>,
    popup: ShellPopupState?,
    commandPalette: CommandPaletteState?,
    draftCommandState: State<String>,
    chatInputState: State<String>,
    onIntent: (ShellIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    BackHandler(
        enabled =
            popup == null && commandPalette == null && appScene == ShellScene.MAIN_SHELL &&
                (
                    mainShellSceneState.chatPanelOpen || mainShellSceneState.interactionSheet != null ||
                        mainShellSceneState.exitOverlay != null
                ),
    ) {
        onIntent(ShellIntent.SystemBackPressed)
    }
    Box(modifier = modifier.fillMaxSize()) {
        if (appScene != ShellScene.MAIN_SHELL) {
            ShellScreenBackdrop()
        }
        when (appScene) {
            ShellScene.LOGIN -> LoginScene(state = entrySceneState, onIntent = onIntent)
            ShellScene.REGISTER -> RegisterScene(state = entrySceneState, onIntent = onIntent)
            ShellScene.WORLD_LOADING ->
                WorldLoadingScene(
                    state = entrySceneState,
                    onIntent = onIntent,
                )
            ShellScene.MAIN_SHELL ->
                CompositionLocalProvider(
                    LocalShellReduceMotion provides mainShellSceneState.reduceMotion,
                ) {
                    MainShellScene(
                        state = mainShellSceneState,
                        statusSnapshotState = statusSnapshotState,
                        chatInputState = chatInputState,
                        onIntent = onIntent,
                    )
                }
        }

        ShellPopupHost(
            popup = popup,
            onDismiss = { onIntent(ShellIntent.PopupDismissed) },
            onActionClick = { action -> onIntent(ShellIntent.PopupActionClicked(action)) },
            onInputChange = { value -> onIntent(ShellIntent.PopupInputChanged(value)) },
        )

        ShellCommandPaletteHost(
            palette = commandPalette,
            draftCommandState = draftCommandState,
            onDismiss = { onIntent(ShellIntent.CommandPaletteDismissed) },
            onCommandChanged = { onIntent(ShellIntent.CommandChanged(it)) },
            onSubmit = { onIntent(ShellIntent.CommandPaletteSendClicked) },
        )
    }
}

@Composable
private fun ShellScreenBackdrop() {
    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .background(ShellBackgroundColor),
    )
}
