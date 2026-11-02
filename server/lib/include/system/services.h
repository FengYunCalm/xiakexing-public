/**
 * @file    services.h
 * @brief   服务守护进程路径统一头文件 - Unified Service Daemon Paths



 *
 * @par Description:
 * 统一所有服务守护进程的路径宏定义。
 * 取代 daemon.h 和 globals.h 中的重复定义。
 * 这些宏只提供稳定路径别名，不保证目标守护已经加载；调用方在 find_object、
 * call_other 或缓存对象引用前，仍需自行处理懒加载、缺失守护和返回值校验。
 *
 */

#pragma strict_types

#ifndef __SYSTEM_SERVICES_H__
#define __SYSTEM_SERVICES_H__

// 核心服务 - Core Services
#ifndef PASSWORD_HASH_D
#define PASSWORD_HASH_D     "/daemons/services/player/password_hash_d"
#endif

#ifndef PLAYER_D
#define PLAYER_D            "/daemons/services/player/player_d"
#endif

#ifndef PLAYER_STATE_D
#define PLAYER_STATE_D      "/daemons/services/player/player_state_d"
#endif

#ifndef PLAYER_LIFECYCLE_D
#define PLAYER_LIFECYCLE_D  "/daemons/services/system/player_lifecycle_d"
#endif

#ifndef PLAYER_ERASURE_D
#define PLAYER_ERASURE_D    "/daemons/services/player/player_erasure_d"
#endif

#ifndef COMBAT_D
#define COMBAT_D            "/daemons/services/gameplay/combat_d"
#endif

#ifndef DEBATE_D
#define DEBATE_D            "/daemons/services/gameplay/debate_d"
#endif

#ifndef COMBAT_LOOT_D
#define COMBAT_LOOT_D       "/daemons/services/gameplay/combat/combat_loot_runtime_d"
#endif

#ifndef COMBAT_LOOT_REGISTRY_D
#define COMBAT_LOOT_REGISTRY_D "/daemons/services/gameplay/combat/combat_loot_registry_d"
#endif

#ifndef COMBAT_EFFECT_D
#define COMBAT_EFFECT_D     "/daemons/services/gameplay/combat/combat_effect_d"
#endif

#ifndef COMBAT_DAMAGE_D
#define COMBAT_DAMAGE_D     "/daemons/services/gameplay/combat/combat_damage_d"
#endif

#ifndef SKILL_D
#define SKILL_D             "/daemons/services/gameplay/skill_d"
#endif

#ifndef SKILL_SOURCE_D
#define SKILL_SOURCE_D      "/daemons/services/gameplay/skill_source_d"
#endif

#ifndef SKILL_SOURCE_REGISTRY_D
#define SKILL_SOURCE_REGISTRY_D "/daemons/services/gameplay/skill_source_registry_d"
#endif

#ifndef SKILL_UNLOCK_D
#define SKILL_UNLOCK_D      "/daemons/services/gameplay/skill_unlock_d"
#endif

#ifndef SKILL_UNLOCK_REGISTRY_D
#define SKILL_UNLOCK_REGISTRY_D "/daemons/services/gameplay/skill_unlock_registry_d"
#endif

#ifndef SKILL_BEHAVIOR_D
#define SKILL_BEHAVIOR_D    "/daemons/services/gameplay/skill_behavior_d"
#endif

#ifndef SKILL_BREAKTHROUGH_D
#define SKILL_BREAKTHROUGH_D "/daemons/services/gameplay/skill_breakthrough_d"
#endif

#ifndef NUMERIC_RULE_D
#define NUMERIC_RULE_D      "/daemons/services/gameplay/numeric_rule_d"
#endif

#ifndef QUEST_D
#define QUEST_D             "/daemons/services/gameplay/quest_d"
#endif

#ifndef QUEST_BLUEPRINT_D
#define QUEST_BLUEPRINT_D   "/daemons/services/gameplay/quest/quest_blueprint_d"
#endif

#ifndef QUEST_BLUEPRINT_REGISTRY_D
#define QUEST_BLUEPRINT_REGISTRY_D "/daemons/services/gameplay/quest/quest_blueprint_registry_d"
#endif

#ifndef QUEST_BLUEPRINT_TEMPLATE_D
#define QUEST_BLUEPRINT_TEMPLATE_D "/daemons/services/gameplay/quest/quest_blueprint_template_d"
#endif

#ifndef EXPLORE_REWARD_D
#define EXPLORE_REWARD_D   "/daemons/services/gameplay/explore_reward_d"
#endif

#ifndef DUNGEON_D
#define DUNGEON_D           "/daemons/services/gameplay/dungeon_d"
#endif

#ifndef DUNGEON_REGISTRY_D
#define DUNGEON_REGISTRY_D  "/daemons/services/gameplay/dungeon/dungeon_registry_d"
#endif

#ifndef DUNGEON_ROOM_D
#define DUNGEON_ROOM_D      "/daemons/services/gameplay/dungeon/dungeon_room"
#endif

#ifndef DUNGEON_INSTANCE_D
#define DUNGEON_INSTANCE_D  "/daemons/services/gameplay/dungeon_instance_d"
#endif

#ifndef DUNGEON_PLAYER_D
#define DUNGEON_PLAYER_D    "/daemons/services/gameplay/dungeon_player_d"
#endif

#ifndef ATTRIBUTE_D
#define ATTRIBUTE_D         "/daemons/services/player/attribute_d"
#endif

#ifndef LIFE_SKILL_D
#define LIFE_SKILL_D        "/daemons/services/gameplay/life_skill_d"
#endif

#ifndef IDLE_RUNTIME_D
#define IDLE_RUNTIME_D      "/daemons/services/gameplay/idle/idle_runtime_d"
#endif

#ifndef LIFE_SKILL_GATHER_D
#define LIFE_SKILL_GATHER_D "/daemons/services/gameplay/life_skill/gather_runtime_d"
#endif

#ifndef RESOURCE_NODE_REGISTRY_D
#define RESOURCE_NODE_REGISTRY_D "/daemons/services/gameplay/life_skill/resource_node_registry_d"
#endif

#ifndef CRAFTING_RECIPE_REGISTRY_D
#define CRAFTING_RECIPE_REGISTRY_D "/daemons/services/gameplay/crafting/crafting_recipe_registry_d"
#endif

#ifndef CRAFTING_RUNTIME_D
#define CRAFTING_RUNTIME_D "/daemons/services/gameplay/crafting/crafting_runtime_d"
#endif

#ifndef MEDICAL_D
#define MEDICAL_D           "/daemons/services/gameplay/medical/medical_d"
#endif

#ifndef ALCHEMY_D
#define ALCHEMY_D           "/daemons/services/gameplay/alchemy/alchemy_d"
#endif

#ifndef ALCHEMY_RECIPE_REGISTRY_D
#define ALCHEMY_RECIPE_REGISTRY_D "/daemons/services/gameplay/alchemy/alchemy_recipe_registry_d"
#endif

#ifndef TAILOR_D
#define TAILOR_D           "/daemons/services/gameplay/tailoring/tailoring_d"
#endif

#ifndef TAILOR_RECIPE_REGISTRY_D
#define TAILOR_RECIPE_REGISTRY_D "/daemons/services/gameplay/tailoring/tailoring_recipe_registry_d"
#endif

#ifndef FORGE_D
#define FORGE_D             "/daemons/services/gameplay/forge/forge_d"
#endif

#ifndef FORGE_RECIPE_REGISTRY_D
#define FORGE_RECIPE_REGISTRY_D "/daemons/services/gameplay/forge/forge_recipe_registry_d"
#endif

// 物品服务 - Item Services
#ifndef QUEST_TRANSITION_D
#define QUEST_TRANSITION_D    "/daemons/services/gameplay/quest/quest_transition_d"
#endif

#ifndef QUEST_PROGRESS_D
#define QUEST_PROGRESS_D      "/daemons/services/gameplay/quest/quest_progress_d"
#endif

#ifndef QUEST_SETTLEMENT_D
#define QUEST_SETTLEMENT_D    "/daemons/services/gameplay/quest/quest_settlement_d"
#endif

#ifndef ITEM_D
#define ITEM_D              "/daemons/services/item/item_d"
#endif

#ifndef ITEM_INVENTORY_D
#define ITEM_INVENTORY_D    "/daemons/services/item/item_inventory"
#endif

#ifndef ITEM_REPAIR_D
#define ITEM_REPAIR_D       "/daemons/services/item/item_repair_d"
#endif

#ifndef SHOP_D
#define SHOP_D              "/daemons/services/item/item_shop"
#endif

#ifndef BANK_D
#define BANK_D              "/daemons/services/economy/bank_d"
#endif

#ifndef ECONOMY_SETTLEMENT_D
#define ECONOMY_SETTLEMENT_D "/daemons/services/economy/economy_settlement_d"
#endif

#ifndef TRADE_D
#define TRADE_D             "/daemons/services/economy/trade_d"
#endif

#ifndef AUCTION_D
#define AUCTION_D           "/daemons/services/economy/auction_d"
#endif

// 社交服务 - Social Services
#ifndef CHAT_D
#define CHAT_D              "/daemons/services/social/chat_d"
#endif

#ifndef GUILD_D
#define GUILD_D             "/daemons/services/social/guild_d"
#endif

#ifndef GUILD_SKILL_D
#define GUILD_SKILL_D       "/daemons/services/social/guild_skill_d"
#endif

#ifndef MAIL_D
#define MAIL_D              "/daemons/services/social/mail_d"
#endif

#ifndef TEAM_D
#define TEAM_D              "/daemons/services/social/team_d"
#endif

#ifndef HEARTBEAT_D
#define HEARTBEAT_D         "/daemons/core/heartbeat_d"
#endif

#ifndef TIMER_D
#define TIMER_D             "/daemons/core/timer_d"
#endif

// 系统服务 - System Services
#ifndef CONFIG_D
#define CONFIG_D            "/daemons/services/system/config/config_d"
#endif

#ifndef REGISTRY_D
#define REGISTRY_D          "/daemons/services/system/registry_d"
#endif

#ifndef LOG_D
#define LOG_D               "/daemons/services/system/log/log_d"
#endif

#ifndef COMMAND_DISPATCH_D
#define COMMAND_DISPATCH_D  "/daemons/services/system/gateway/command_dispatch_d"
#endif

#ifndef GATEWAY_TELEMETRY_D
#define GATEWAY_TELEMETRY_D "/daemons/services/system/gateway/gateway_telemetry_d"
#endif

#ifndef MEASUREMENT_SNAPSHOT_D
#define MEASUREMENT_SNAPSHOT_D "/daemons/services/system/gateway/measurement_snapshot_d"
#endif

#ifndef COMMAND_OWNER_TXN_D
#define COMMAND_OWNER_TXN_D "/daemons/services/system/gateway/command_owner_txn_d"
#endif

#ifndef QUEST_PROTOCOL
#define QUEST_PROTOCOL      "/daemons/services/gameplay/quest/quest_protocol"
#endif

#ifndef CLIENT_SYNC_D
#define CLIENT_SYNC_D       "/daemons/services/system/sync/client_sync_d"
#endif

#ifndef MESSAGE_D
#define MESSAGE_D           "/daemons/services/system/message_d"
#endif

#ifndef WORLD_MAP_D
#define WORLD_MAP_D         "/daemons/services/world/world_map_d"
#endif

#ifndef CLIENT_PROJECTION_D
#define CLIENT_PROJECTION_D "/daemons/services/system/sync/client_projection_d"
#endif

#ifndef ROOM_VIEW_D
#define ROOM_VIEW_D         "/daemons/services/system/room_view_d"
#endif

#ifndef DIALOG_PROJECTION_D
#define DIALOG_PROJECTION_D "/daemons/services/system/sync/dialog_projection_d"
#endif

#ifndef DATA_MAINTENANCE_D
#define DATA_MAINTENANCE_D  "/daemons/services/system/data_maintenance_d"
#endif

#ifndef ANNOUNCE_D
#define ANNOUNCE_D          "/daemons/services/system/log/announce_d"
#endif
#ifndef TIME_D
#define TIME_D              "/daemons/services/world/time_d"
#endif
#ifndef NPC_SCHEDULE_D
#define NPC_SCHEDULE_D      "/daemons/services/world/npc_schedule_d"
#endif
#ifndef WORLD_EVENT_D
#define WORLD_EVENT_D       "/daemons/services/world/world_event_d"
#endif
#ifndef MERIDIAN_D
#define MERIDIAN_D          "/daemons/services/gameplay/meridian_d"
#endif
#ifndef SHUTDOWN_D
#define SHUTDOWN_D          "/daemons/services/system/shutdown_d"
#endif

#ifndef WORKER_BUS_D
#define WORKER_BUS_D         "/daemons/services/system/multicore/worker_bus_d"
#endif

#ifndef MULTICORE_CONFIG_D
#define MULTICORE_CONFIG_D   "/daemons/services/system/multicore/multicore_config_d"
#endif

#ifndef WORKER_DRIVER_ADAPTER
#define WORKER_DRIVER_ADAPTER "/daemons/services/system/multicore/worker_driver_adapter"
#endif

#ifndef LOGIN_SESSION
#define LOGIN_SESSION         "/kernel/login/login_session"
#endif

#ifndef SIMUL_EFUN_LOG_HELPER
#define SIMUL_EFUN_LOG_HELPER "/kernel/simul_efun_log"
#endif

#ifndef XK_PROTOCOL_CORE
#define XK_PROTOCOL_CORE      "/kernel/framework/xk_protocol_core"
#endif

#ifndef ASYNC_COMPUTE_D
#define ASYNC_COMPUTE_D      "/daemons/services/system/multicore/async_compute_d"
#endif

#ifndef PROPOSAL_COMMIT_D
#define PROPOSAL_COMMIT_D    "/daemons/services/system/multicore/proposal_commit_d"
#endif

#ifndef READ_SUMMARY_D
#define READ_SUMMARY_D       "/daemons/services/system/read_summary_d"
#endif

#ifndef OWNER_ROUTE_D
#define OWNER_ROUTE_D        "/daemons/services/system/multicore/owner_route_d"
#endif

#ifndef OWNER_RUNTIME_D
#define OWNER_RUNTIME_D      "/daemons/services/system/multicore/owner_runtime_d"
#endif

#ifndef OWNER_MESSAGE_BUS_D
#define OWNER_MESSAGE_BUS_D  "/daemons/services/system/multicore/owner_message_bus_d"
#endif

#ifndef OWNER_COMMIT_D
#define OWNER_COMMIT_D       "/daemons/services/system/multicore/owner_commit_d"
#endif

#ifndef OWNER_SESSION_FIFO_D
#define OWNER_SESSION_FIFO_D "/daemons/services/system/multicore/owner_session_fifo_d"
#endif

#ifndef OWNER_PERSISTENCE_D
#define OWNER_PERSISTENCE_D  "/daemons/services/system/multicore/owner_persistence_d"
#endif

#ifndef OWNER_GAMEPLAY_D
#define OWNER_GAMEPLAY_D     "/daemons/services/system/multicore/owner_gameplay_d"
#endif

#ifndef TEST_FIXTURE_D
#define TEST_FIXTURE_D       "/daemons/services/system/test_fixture_d"
#endif

#ifndef XK_TRANSPORT_D
#define XK_TRANSPORT_D      "/daemons/services/system/gateway/xk_transport_d"
#endif

#ifndef GATEWAY_SESSION_D
#define GATEWAY_SESSION_D    "/daemons/services/system/gateway/gateway_session_d"
#endif

#ifndef RANK_D
#define RANK_D              "/daemons/services/player/rank_d"
#endif

#ifndef PLAYER_ACTOR_ANALYSIS_D
#define PLAYER_ACTOR_ANALYSIS_D "/daemons/services/player/player_actor_analysis_d"
#endif

#ifndef SECT_D
#define SECT_D              "/daemons/services/player/sect_d"
#endif


#ifndef SECT_SKILL_D
#define SECT_SKILL_D        "/daemons/services/player/sect_skill_d"
#endif

#ifndef SECURITY_MONITOR_D
#define SECURITY_MONITOR_D  "/daemons/services/player/security_monitor_d"
#endif

#ifndef PROGRESSION_D
#define PROGRESSION_D       "/daemons/services/player/progression_d"
#endif

#ifndef GROWTH_D
#define GROWTH_D            "/daemons/services/player/growth_d"
#endif

#ifndef GROWTH_SCHEMA_D
#define GROWTH_SCHEMA_D     "/daemons/services/player/growth_schema_d"
#endif

#ifndef REPUTATION_D
#define REPUTATION_D        "/daemons/services/player/reputation_d"
#endif

#ifndef REWARD_APPLY_D
#define REWARD_APPLY_D      "/daemons/services/player/reward_apply_d"
#endif

#ifndef REWARD_SETTLEMENT_D
#define REWARD_SETTLEMENT_D "/daemons/services/player/reward_settlement_d"
#endif

#ifndef ENTITLEMENT_D
#define ENTITLEMENT_D       "/daemons/services/player/entitlement_d"
#endif

#ifndef REWARD_FEEDBACK_D
#define REWARD_FEEDBACK_D   "/daemons/services/player/reward_feedback_d"
#endif

#ifndef ECONOMY_FEEDBACK_D
#define ECONOMY_FEEDBACK_D  "/daemons/services/economy/economy_feedback_d"
#endif

#ifndef ITEM_FEEDBACK_D
#define ITEM_FEEDBACK_D     "/daemons/services/item/item_feedback_d"
#endif

// World services
#ifndef REGION_D
#define REGION_D            "/daemons/services/world/region_d"
#endif

#ifndef SPAWN_D
#define SPAWN_D             "/daemons/services/world/spawn_d"
#endif

#ifndef STORY_D
#define STORY_D             "/daemons/services/world/story_d"
#endif

#ifndef NPC_BEHAVIOR_D
#define NPC_BEHAVIOR_D      "/daemons/services/world/npc_behavior_d"
#endif

#ifndef ROOM_BEHAVIOR_D
#define ROOM_BEHAVIOR_D     "/daemons/services/world/room_behavior_d"
#endif

#ifndef DIALOG_D
#define DIALOG_D            "/daemons/services/world/dialog_d"
#endif

#ifndef NPC_VIEW_D
#define NPC_VIEW_D          "/daemons/services/world/npc_view_d"
#endif

#ifndef WORLD_CONTEXT_D
#define WORLD_CONTEXT_D     "/daemons/services/world/world_context_d"
#endif

#ifndef INSTANCE_ROOM_D
#define INSTANCE_ROOM_D     "/daemons/services/world/instance/instance_room_d"
#endif

#ifndef LOGIN_PROJECTION_D
#define LOGIN_PROJECTION_D  "/daemons/services/player/login_projection_d"
#endif

#ifndef MIGRATION_D
#define MIGRATION_D         "/daemons/services/system/migration_d"
#endif

#ifndef WORLD_TRAVEL_D
#define WORLD_TRAVEL_D      "/daemons/services/world/world_travel_d"
#endif

#ifndef CONTENT_MODE_POLICY_D
#define CONTENT_MODE_POLICY_D "/daemons/services/world/content_mode_policy_d"
#endif

#ifndef WORLD_STATE_D
#define WORLD_STATE_D       "/daemons/services/world/world_state_d"
#endif

#ifndef PROTOCOL_SEQUENCE_D
#define PROTOCOL_SEQUENCE_D "/daemons/services/system/protocol_sequence_d"
#endif

#ifndef WORLD_ORCHESTRATOR_D
#define WORLD_ORCHESTRATOR_D "/daemons/services/world/world_orchestrator_d"
#endif

#ifndef WORLD_LIFECYCLE_D
#define WORLD_LIFECYCLE_D   "/daemons/services/world/world_lifecycle_d"
#endif

#ifndef WORLD_ENTITY_LIFECYCLE_D
#define WORLD_ENTITY_LIFECYCLE_D "/daemons/services/world/world_entity_lifecycle_d"
#endif

#ifndef WORLD_PROJECTION_D
#define WORLD_PROJECTION_D "/daemons/services/world/world_projection_d"
#endif

#ifndef WORLD_OBSERVER_D
#define WORLD_OBSERVER_D "/daemons/services/world/world_observer_d"
#endif

#ifndef CONTENT_REGISTRY_D
#define CONTENT_REGISTRY_D  "/daemons/services/world/content_registry_d"
#endif

#ifndef HOUSING_D
#define HOUSING_D           "/daemons/services/world/housing_d"
#endif

#ifndef QUEST_ACTOR_D
#define QUEST_ACTOR_D       "/daemons/services/world/quest_actor_d"
#endif

#ifndef QUEST_SCENE_D
#define QUEST_SCENE_D       "/daemons/services/gameplay/quest_scene_d"
#endif

#ifndef ROOM_D
#define ROOM_D              0
#endif

#ifndef MAP_D
#define MAP_D               WORLD_MAP_D
#endif

#ifndef NPC_D
#define NPC_D               0
#endif

// 经济服务 - Economy Services
#ifndef MONEY_D
#define MONEY_D             "/daemons/services/economy/money_d"
#endif


// 其他服务 - Other Services
#ifndef ACTIVITY_D
#define ACTIVITY_D          0         /* 当前仓库未提供活动守护真源 */
#endif

#ifndef NAME_D
#define NAME_D              "/daemons/services/system/name_d"
#endif

#ifndef TEMP_D
#define TEMP_D              0         /* 明确表示当前无稳定临时守护路径，调用前必须判空 */
#endif

#ifndef MONITOR_D
#define MONITOR_D           "/daemons/core/monitor_d"
#endif

#ifndef ALERT_D
#define ALERT_D             "/daemons/core/alert_d"
#endif

#ifndef HEARTBEAT_D
#define HEARTBEAT_D         "/daemons/core/heartbeat_d"
#endif

#endif /* __SYSTEM_SERVICES_H__ */
