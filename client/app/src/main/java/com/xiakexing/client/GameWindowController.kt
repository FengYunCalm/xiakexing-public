package com.xiakexing.client

import android.view.Window
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat

internal class GameWindowController(
    private val window: Window,
) {
    private var immersiveEnabled: Boolean = false

    init {
        applyWindowMode()
    }

    fun setImmersiveEnabled(enabled: Boolean) {
        immersiveEnabled = enabled
        applyWindowMode()
    }

    fun onResume() {
        applyWindowMode()
    }

    fun onWindowFocusChanged(hasFocus: Boolean) {
        if (hasFocus) {
            applyWindowMode()
        }
    }

    private fun applyWindowMode() {
        // 软输入模式以 AndroidManifest（adjustResize）为唯一属主，此处不再覆盖；
        // 理由：edge-to-edge 下 API≥30 该配置本为 no-op、ime insets 直发；
        // API<30 需靠它派发 insets，程序侧重复设置只会造成漂移。
        val controller = WindowCompat.getInsetsController(window, window.decorView)
        if (immersiveEnabled) {
            controller.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            controller.hide(WindowInsetsCompat.Type.systemBars())
        } else {
            controller.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_DEFAULT
            controller.show(WindowInsetsCompat.Type.systemBars())
        }
    }
}
