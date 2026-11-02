package com.xiakexing.client.feature.shell

enum class ShellScene {
    LOGIN,
    REGISTER,
    WORLD_LOADING,
    MAIN_SHELL,
}

enum class InteractionObjectKind {
    NPC,
    ITEM,
    SCENE,
}

enum class DockButtonType {
    CHARACTER,
    ACTION,
    WORLD,
    QUEST,
    MAP,
    INPUT,
}

enum class TitleActionType {
    MESSAGE,
    SYSTEM,
}

enum class ExitOverlayType {
    COMMON,
    COMBAT,
}

enum class PopupSource {
    INTERACTION_OBJECT,
    INTERACTION_GIVE,
    INTERACTION_LEARN,
    DOCK_BUTTON,
    TITLE_ACTION,
    PROTOCOL,
}

enum class InteractionSheetMode {
    DETAIL,
    CONVERSATION,
    GIVE_ITEMS,
}

enum class ChatChannelKind {
    NEARBY,
    WORLD,
    PRIVATE,
}

enum class ChatDirection {
    SELF,
    INCOMING,
    SYSTEM,
}
