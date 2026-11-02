package com.xiakexing.client.feature.shell

/**
 * InteractionSession 域 dialog 判定纯函数——quest 空态/详情标题/
 * 任务查询命令分类（ShellDialogPacketCoordinator 收敛）。
 */
internal fun List<ShellActionGroup>.noConcreteQuestActions(): Boolean {
    return asSequence()
        .flatMap { group -> group.actions.asSequence() }
        .none { action -> action.isConcreteQuestFlowAction() }
}

internal fun String.isNpcQuestProviderQueryCommand(): Boolean {
    val normalized = trim().lowercase()
    return normalized.startsWith("quest ") &&
        !normalized.isQuestInfoCommand() &&
        !normalized.isNpcQuestMutationCommand()
}

internal const val NPC_QUEST_EMPTY_MESSAGE = "此人眼下没有可处理的任务"
internal const val NPC_LEARNING_EMPTY_MESSAGE = "此人暂时没有可以学习的技能"

internal fun String.isGenericQuestDetailTitle(): Boolean {
    return trim() in setOf("任务详情", "人物委托", "任务")
}
