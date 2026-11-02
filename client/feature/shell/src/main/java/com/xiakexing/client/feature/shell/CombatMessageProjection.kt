package com.xiakexing.client.feature.shell

/** 战斗命令错误与结构化错误消息共享一条主文本区去重路径。 */
internal fun appendCombatActionFeedback(
    message: String,
    fromErrorPacket: Boolean,
    projection: MessageProjectionState,
    appendNarrative: (String) -> Unit,
) {
    val plan =
        if (fromErrorPacket) {
            // XK_ERRO is the authoritative event for combat command failures; each tap is distinct.
            StructuredEchoPlan(append = true, markSuppressions = 0)
        } else {
            buildStructuredVisiblePlan(
                message = message,
                pendingEcho = projection.pendingStructuredMessageEcho,
                suppressions = projection.pendingStructuredMessageEchoSuppressions,
                recentTranscriptLines = projection.recentTranscriptLines,
            )
        }
    projection.applyStructuredEchoPlan(message, plan)
    if (plan.append) {
        appendNarrative(message)
    }
}
