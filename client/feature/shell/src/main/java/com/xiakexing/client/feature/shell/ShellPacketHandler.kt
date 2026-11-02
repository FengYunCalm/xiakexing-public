package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.IncomingFrame
import com.xiakexing.client.core.protocol.XkMessageType
import com.xiakexing.client.core.protocol.payload.LoginPrompt
import com.xiakexing.client.core.protocol.payload.RegisterPrompt

/**
 * ShellPacketHandler——只查 PacketOwnerRoute 并调用 typed entry，
 * 不再保留 40 分支 when 链。登录语义（LOGN 分流/自动重试守卫）与
 * 未接入包回退保留在此（VM 桥接面）。
 */
internal class ShellPacketHandler(
    private val route: PacketOwnerRoute,
    private val sink: Sink,
) {
    interface Sink {
        fun shouldStopAutomaticLoginRetry(): Boolean

        fun currentLoginFailureMessage(): String

        fun showLoginError(message: String)

        fun onLoginPromptReady()

        fun onLoginBootstrapComplete()

        fun sendLoginForPrompt()

        fun onRegisterPrompt(prompt: RegisterPrompt)

        fun handleUnhandledPacket(frame: IncomingFrame.Packet)
    }

    fun handle(
        frame: IncomingFrame.Packet,
        preparedPayload: PreparedProtocolPayload? = null,
    ) {
        // 登录包语义（自动重试守卫 + bootstrap 分流）保留在 handler。
        if (frame.envelope.type == XkMessageType.LOGN) {
            handleLoginPacket(LoginPrompt.fromJson(frame.envelope.payload))
            return
        }
        if (frame.envelope.type == XkMessageType.REGI) {
            sink.onRegisterPrompt(RegisterPrompt.fromJson(frame.envelope.payload))
            return
        }
        val entry = route.resolve(frame.envelope.type)
        if (entry == null) {
            sink.handleUnhandledPacket(frame)
            return
        }
        entry.dispatch(frame, preparedPayload)
    }

    private fun handleLoginPrompt() {
        if (sink.shouldStopAutomaticLoginRetry()) {
            sink.showLoginError(sink.currentLoginFailureMessage())
            return
        }
        sink.onLoginPromptReady()
        sink.sendLoginForPrompt()
    }

    private fun handleLoginPacket(packet: LoginPrompt) {
        when {
            packet.requestsCredentials() -> handleLoginPrompt()
            packet.normalizedAction() == LOGIN_ACTION_BOOTSTRAP_COMPLETE -> sink.onLoginBootstrapComplete()
        }
    }
}
