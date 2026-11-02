package com.xiakexing.client.feature.shell

/**
 * Combat perform 面板的加载与延迟打开状态。
 * 技能列表回包前不创建半成品面板，避免空槽与动作文字分两帧出现。
 */
internal class SkillCombatActionState {
    private var loaded = false
    private var pendingOpen = false
    private var pendingRequiresCombat = false

    fun reset() {
        loaded = false
        cancelPendingOpen()
    }

    fun markLoaded() {
        loaded = true
    }

    fun requestOpen(
        requireCombat: Boolean,
        waitForRefresh: Boolean = false,
    ): Boolean {
        pendingOpen = waitForRefresh || !loaded
        pendingRequiresCombat = requireCombat
        return pendingOpen
    }

    fun consumePendingOpen(combatActive: Boolean): Boolean {
        val shouldOpen = pendingOpen && (!pendingRequiresCombat || combatActive)
        cancelPendingOpen()
        return shouldOpen
    }

    fun hasPendingOpen(): Boolean = pendingOpen

    fun cancelPendingOpen() {
        pendingOpen = false
        pendingRequiresCombat = false
    }
}
