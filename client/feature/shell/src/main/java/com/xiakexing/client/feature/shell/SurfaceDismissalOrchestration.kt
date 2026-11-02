package com.xiakexing.client.feature.shell

import com.xiakexing.client.feature.shell.component.isNpcInlineInteractionSheet
import com.xiakexing.client.feature.shell.popup.isNpcRootMode

/**
 * Surface 域 dismissal 编排——系统返回、弹窗/面板关闭与表面清理
 * （ShellSurfaceDismissalCoordinator 收敛）。动作确认状态字段归
 * SurfaceCoordinator 经 SurfaceSink 读写；纯状态变换在 SurfaceStateReducer.kt。
 */
internal fun handleBack(sink: SurfaceSink): Boolean {
    val state = sink.currentState()
    when {
        state.dialog != null && state.dialog.allowsSystemBackDismiss() -> dismissPopup(sink)
        state.dialog != null -> return true
        state.commandPalette != null -> sink.dismissCommandPalette()
        state.chatPanelOpen -> dismissTransientInteractionSurfaces(sink)
        state.sheet != null -> {
            if (!sink.popSheet()) dismissSheet(sink)
        }
        state.exitOverlay != null -> sink.dismissExitOverlay()
        else -> return false
    }
    return true
}

internal fun dismissSheet(sink: SurfaceSink) {
    sink.clearPendingInteractionRequest()
    sink.clearPendingProtocolSheetRequest()
    sink.clearPendingInventoryInspectItem()
    // 方案 R-35（M08）：NPC 二级 sheet（任务/技能/给予等）关闭后，
    // 目标 NPC 仍在房间（会话身份有效）时恢复 NPC 上下文板；其余清场路径直接 copy(sheet=null) 不经此处。
    val state = sink.currentState()
    val currentSheet = state.sheet
    if (currentSheet != null && currentSheet.isNpcInlineInteractionSheet() &&
        !currentSheet.isNpcRootMode() && !currentSheet.isNpcNarrativeConversationSheet()
    ) {
        val target =
            state.interactionObjects.findInteractionObjectByKey(
                currentSheet.interactionTargetUiKey,
            )
        if (target != null && target.kind == InteractionObjectKind.NPC) {
            sink.updateState {
                copy(
                    sheet =
                        buildNpcContextResumeSheet(
                            target,
                            state.hasGiveableInventoryItems(),
                        ),
                )
            }
            return
        }
    }
    sink.updateState { copy(sheet = null) }
}

internal fun dismissPopup(sink: SurfaceSink) {
    val parentPopup = sink.currentState().dialog?.parent
    if (parentPopup != null) {
        sink.clearPendingProtocolPopupRequest()
        sink.updateState { copy(dialog = parentPopup) }
        return
    }
    sink.clearDismissSheetOnPopupAction()
    sink.clearPendingProtocolPopupRequest()
    sink.clearPendingInventoryInspectItem()
    sink.clearBankQuantityState()
    sink.updateState { copy(dialog = null) }
}

internal fun dismissPopupGroup(sink: SurfaceSink) {
    sink.clearDismissSheetOnPopupAction()
    sink.clearPendingProtocolPopupRequest()
    sink.clearPendingInventoryInspectItem()
    sink.clearBankQuantityState()
    sink.updateState { copy(dialog = null) }
}

internal fun dismissTransientInteractionSurfaces(sink: SurfaceSink) {
    sink.clearPendingInteractionRequest()
    sink.clearPendingProtocolSheetRequest()
    sink.clearPendingProtocolPopupRequest()
    sink.clearDismissSheetOnPopupAction()
    sink.clearPendingInventoryInspectItem()
    sink.clearBankQuantityState()
    sink.updateState { dismissTransientInteractionSurfacesState() }
}

internal fun dismissObscuringSurfacesAroundInteractionSheet(sink: SurfaceSink) {
    sink.clearPendingProtocolPopupRequest()
    sink.clearDismissSheetOnPopupAction()
    sink.clearPendingInventoryInspectItem()
    sink.clearQuantityFlowState()
    sink.updateState {
        copy(
            dialog = null,
            commandPalette = null,
            exitOverlay = null,
            chatPanelOpen = false,
        )
    }
}

internal fun dismissObscuringSurfacesForExternalCommand(sink: SurfaceSink) {
    sink.clearPendingInteractionRequest()
    sink.clearDismissSheetOnPopupAction()
    sink.clearPendingInventoryInspectItem()
    sink.clearBankQuantityState()
    sink.updateState { dismissTransientInteractionSurfacesState() }
}

internal fun dismissObscuringSurfacesForCombatStart(sink: SurfaceSink) {
    sink.clearPendingInteractionRequest()
    sink.clearDismissSheetOnPopupAction()
    sink.clearPendingInventoryInspectItem()
    sink.clearBankQuantityState()
    sink.updateState {
        copy(
            sheet = sheet?.takeIf { it.shouldRemainVisibleDuringCombatStart() },
            dialog = null,
            commandPalette = null,
            exitOverlay = exitOverlay?.takeIf { it.type == ExitOverlayType.COMBAT },
            chatPanelOpen = false,
        )
    }
}

internal fun dismissSheetIfShowing(sink: SurfaceSink) {
    if (sink.currentState().sheet != null) {
        dismissSheet(sink)
    }
}
