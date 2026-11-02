package com.xiakexing.client.feature.shell

/**
 * Combat 域 Sink——编排层向 VM/其他域暴露的副作用接口
 * （SkillSink/MessageCoordinatorSink 先例，独立文件）。
 */
internal interface CombatSink {
    fun dismissObscuringSurfacesForCombatStart()

    fun refreshCombatExitOverlayIfVisible()

    fun openCombatActionHubPanel()

    fun currentState(): ShellUiState

    fun appendCombatTranscript(line: String)

    fun updateState(transform: ShellUiState.() -> ShellUiState)
}
