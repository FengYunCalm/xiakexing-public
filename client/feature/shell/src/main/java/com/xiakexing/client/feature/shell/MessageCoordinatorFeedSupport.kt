package com.xiakexing.client.feature.shell

internal fun MessageCoordinator.appendNarrativeTranscript(line: String) {
    if (line.isBlank() || line.isLoginSuccessStatusLine() || line.isPromptOnlyLine()) return
    if (isRecentTranscriptDuplicate(line)) return
    appendTranscriptToFeed(line)
}

internal fun MessageCoordinator.appendErrorFeedback(message: String) {
    if (message.isBlank()) return
    appendTranscript(message)
}

internal fun MessageCoordinator.appendStructuredErrorFeedback(message: String): Boolean {
    val plan =
        buildStructuredErrorFeedbackPlan(
            message = message,
            pendingEcho = projection.pendingStructuredMessageEcho,
            suppressions = projection.pendingStructuredMessageEchoSuppressions,
            recentTranscriptLines = projection.recentTranscriptLines,
        )
    projection.applyStructuredEchoPlan(message, plan)
    if (plan.append) appendErrorFeedback(message)
    return plan.append
}

/* 第七轮：任务进度/完成反馈直达主文本区——去重后直接进 feed，
 * 不走 appendTranscript 的交互捕获分类。 */
internal fun MessageCoordinator.appendQuestProgressToTranscript(message: String) {
    if (!buildQuestProgressPlan(message, projection.recentTranscriptLines)) return
    appendTranscriptToFeed(message)
}

internal fun MessageCoordinator.appendStructuredVisibleMessage(message: String): Boolean {
    val plan =
        buildStructuredVisiblePlan(
            message = message,
            pendingEcho = projection.pendingStructuredMessageEcho,
            suppressions = projection.pendingStructuredMessageEchoSuppressions,
            recentTranscriptLines = projection.recentTranscriptLines,
        )
    projection.applyStructuredEchoPlan(message, plan)
    if (plan.append) appendTranscript(message)
    return plan.append
}

internal fun MessageCoordinator.appendTranscriptToFeed(line: String) {
    projection.appendTranscript(line)
    val plan =
        buildTranscriptAppendPlan(
            line,
            projection.recentTranscriptLines,
            sink.currentState().visibleTranscript,
        )
    sink.updateState(buildVisibleTranscriptAppendTransform(plan.visibleTranscript))
    sink.markPendingMoveAutoScrollActivity()
}

internal fun MessageCoordinator.isRecentTranscriptDuplicate(line: String): Boolean =
    isRecentTranscriptDuplicate(line, projection.recentTranscriptLines)

internal fun MessageCoordinator.appendNotice(line: String) {
    if (line.isLoginSuccessStatusLine()) return
    val normalized = line.normalizeInteractionLine() ?: return
    if (normalized.isBlank()) return
    projection.updateNoticeFeed(normalized)
}

internal fun MessageCoordinator.appendChatTranscriptAndNotice(
    line: String,
    duplicateRawEcho: String? = null,
) {
    if (line.isLoginSuccessStatusLine()) return
    val normalized = line.normalizeInteractionLine() ?: return
    projection.appendTranscript(normalized)
    projection.appendNotice(normalized)
    val plan =
        buildChatTranscriptAppendPlan(
            line = normalized,
            duplicateRawEcho = duplicateRawEcho,
            currentTranscript = projection.recentTranscriptLines,
            visibleTranscript = sink.currentState().visibleTranscript,
        )
    sink.updateState(buildVisibleTranscriptAppendTransform(plan.visibleTranscript))
    sink.markPendingMoveAutoScrollActivity()
}

internal fun MessageCoordinator.appendTranscriptAndNotice(line: String) {
    if (line.isBlank() || line.isLoginSuccessStatusLine()) return
    projection.appendTranscript(line)
    projection.appendNotice(line)
    val plan =
        buildTranscriptAppendPlan(
            line,
            projection.recentTranscriptLines,
            sink.currentState().visibleTranscript,
        )
    sink.updateState(buildVisibleTranscriptAppendTransform(plan.visibleTranscript))
    sink.markPendingMoveAutoScrollActivity()
}
