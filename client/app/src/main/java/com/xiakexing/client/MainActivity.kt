package com.xiakexing.client

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.ui.Modifier
import com.xiakexing.client.core.network.ConnectionAttachment
import com.xiakexing.client.core.network.ProcessConnectionOwnerProvider
import com.xiakexing.client.feature.shell.ShellRoute

class MainActivity : ComponentActivity() {
    private lateinit var windowController: GameWindowController
    private var uiAttachment: ConnectionAttachment? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        windowController = GameWindowController(window)
        setContent {
            ClientNewTheme {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background,
                ) {
                    Box(modifier = Modifier.fillMaxSize()) {
                        ShellRoute(onImmersiveModeChanged = windowController::setImmersiveEnabled)
                    }
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        windowController.onResume()
    }

    override fun onStart() {
        super.onStart()
        val owner =
            (application as? ProcessConnectionOwnerProvider)?.processConnectionOwner
                ?: error("Application must provide ProcessConnectionOwner")
        uiAttachment = owner.attachUi()
        com.xiakexing.client.connect.GameConnectionService.start(this)
    }

    override fun onStop() {
        uiAttachment?.close()
        uiAttachment = null
        super.onStop()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (::windowController.isInitialized) {
            windowController.onWindowFocusChanged(hasFocus)
        }
    }
}
