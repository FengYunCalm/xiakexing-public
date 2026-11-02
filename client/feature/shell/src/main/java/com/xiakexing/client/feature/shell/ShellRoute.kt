package com.xiakexing.client.feature.shell

import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.ViewModelProvider.AndroidViewModelFactory.Companion.APPLICATION_KEY
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.lifecycle.viewmodel.initializer
import androidx.lifecycle.viewmodel.viewModelFactory

@Composable
fun ShellRoute(
    // Kotlin 默认参数不生成单参构造，默认 AndroidViewModelFactory 反射必炸（NoSuchMethodException）；
    // 凡带默认参数的 ViewModel 必须显式 factory。
    viewModel: ShellViewModel =
        viewModel(
            factory =
                viewModelFactory {
                    initializer {
                        ShellViewModel(this[APPLICATION_KEY]!!)
                    }
                },
        ),
    onImmersiveModeChanged: (Boolean) -> Unit = {},
) {
    val appScene by viewModel.derivedAppScene.collectAsStateWithLifecycle()
    val entrySceneState by viewModel.derivedEntryScene.collectAsStateWithLifecycle()
    val mainShellSceneState by viewModel.derivedMainScene.collectAsStateWithLifecycle()
    val statusSnapshotState = viewModel.derivedStatusSnapshot.collectAsStateWithLifecycle()
    val dialog by viewModel.derivedDialog.collectAsStateWithLifecycle()
    val commandPalette by viewModel.derivedCommandPalette.collectAsStateWithLifecycle()
    val commandInputState = viewModel.derivedCommandInput.collectAsStateWithLifecycle()
    val chatInputState = viewModel.derivedChatInput.collectAsStateWithLifecycle()
    val lifecycleOwner = LocalLifecycleOwner.current
    DisposableEffect(lifecycleOwner, viewModel) {
        val observer =
            LifecycleEventObserver { _, event ->
                when (event) {
                    Lifecycle.Event.ON_START -> viewModel.onIntent(ShellIntent.AppForegrounded)
                    Lifecycle.Event.ON_STOP -> viewModel.onIntent(ShellIntent.AppBackgrounded)
                    else -> Unit
                }
            }
        lifecycleOwner.lifecycle.addObserver(observer)
        if (lifecycleOwner.lifecycle.currentState.isAtLeast(Lifecycle.State.STARTED)) {
            viewModel.onIntent(ShellIntent.AppForegrounded)
        }
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(observer)
        }
    }
    val immersiveModeCallback by rememberUpdatedState(onImmersiveModeChanged)
    val intentHandler = remember(viewModel) { viewModel::onIntent }
    LaunchedEffect(appScene) {
        immersiveModeCallback(
            appScene == ShellScene.MAIN_SHELL || appScene == ShellScene.WORLD_LOADING,
        )
    }
    ShellScreen(
        appScene = appScene,
        entrySceneState = entrySceneState,
        mainShellSceneState = mainShellSceneState,
        statusSnapshotState = statusSnapshotState,
        popup = dialog,
        commandPalette = commandPalette,
        draftCommandState = commandInputState,
        chatInputState = chatInputState,
        onIntent = intentHandler,
    )
}
