/**
 * @file    registry_d.c
 * @brief   系统注册表服务



 *
 * @par Description:
 * 系统服务文件，承担当前 canonical system 服务职责。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/services.h>

inherit "/lib/framework/daemon";

#define HEALTH_SERVICE   "/daemons/services/system/log/health_d"
#define REGISTRY_SERVICE "/daemons/services/system/registry_d"
#define SOCIAL_SERVICE   "/daemons/services/social/social_d"

#define SERVICE_ACTIVE      "active"
#define SERVICE_INACTIVE    "inactive"
#define SERVICE_DEGRADED    "degraded"
#define SERVICE_FAILED      "failed"

nosave mapping _services;
nosave mapping _service_metadata;
nosave mapping _service_stats;
nosave mapping _dependency_graph;

private void register_service_catalog(mapping catalog);
void register_core_services();
void register_system_services();
void register_game_services();
void auto_register_services();
void register_service(string name, string path, mapping metadata);
void unregister_service(string name);
string discover_service(string name);
object get_service(string name);
void update_service_status(string name, string status);
mapping query_service(string name);
mapping query_all_services();
mapping query_services_by_type(string type);
string *list_services();
mapping query_dependency_graph();
mapping query_service_readiness(string name);
private void rebuild_dependency_graph();

void create()
{
    ::create();

    _services = ([]);
    _service_metadata = ([]);
    _service_stats = ([]);
    _dependency_graph = ([
        "ok": 0,
        "nodes": ([]),
        "edges": ([]),
        "missing_dependencies": ([]),
        "cycles": ({}),
        "topological_order": ({}),
        "error": "service catalog has not been registered",
    ]);
    auto_register_services();
    rebuild_dependency_graph();
    log_file("system/service", sprintf("[%s] Service registry initialized\n", ctime(time())[4..19]));
}

private void register_service_catalog(mapping catalog)
{
    string service_name;
    mapping service_def;

    if (!mapp(catalog))
        return;

    foreach (service_name, service_def in catalog) {
        if (!stringp(service_name) || !mapp(service_def))
            continue;

        register_service(service_name, service_def["path"], service_def["metadata"]);
    }
}

void register_core_services()
{
    mapping services;

    /*
     * registry 将服务分成 core / system / game 三层，
     * 不是为了文档好看，而是为了让依赖关系和启动顺序更可控：
     * core 先于 game 注册，system 则作为横切能力被 core/game 共同依赖。
     */
    services = ([
        "password_hash": (["path": PASSWORD_HASH_D, "metadata": (["type": "core", "description": "账号密码哈希与迁移服务", "dependencies": ({})])]),
        "player": (["path": PLAYER_D, "metadata": (["type": "core", "description": "玩家管理服务", "dependencies": ({"password_hash"})])]),
        "attribute": (["path": ATTRIBUTE_D, "metadata": (["type": "core", "description": "属性服务", "dependencies": ({"player"})])]),
        "progression": (["path": PROGRESSION_D, "metadata": (["type": "core", "description": "成长规则服务", "dependencies": ({"player", "attribute"})])]),
        "reputation": (["path": REPUTATION_D, "metadata": (["type": "core", "description": "声望规则服务", "dependencies": ({"player"})])]),
        "reward_apply": (["path": REWARD_APPLY_D, "metadata": (["type": "core", "description": "奖励事务编排服务", "dependencies": ({"player", "progression", "item", "money"})])]),
        "reward_settlement": (["path": REWARD_SETTLEMENT_D, "metadata": (["type": "core", "description": "跨模式幂等奖励结算账本", "dependencies": ({"player", "reward_apply", "entitlement"})])]),
        "entitlement": (["path": ENTITLEMENT_D, "metadata": (["type": "core", "description": "跨模式内容解锁账本", "dependencies": ({"player"})])]),
        "reward_feedback": (["path": REWARD_FEEDBACK_D, "metadata": (["type": "core", "description": "奖励反馈/刷新辅助服务", "dependencies": ({"reward_apply", "item", "client_sync"})])]),
        "item": (["path": ITEM_D, "metadata": (["type": "core", "description": "物品管理服务", "dependencies": ({"player"})])]),
        "item_feedback": (["path": ITEM_FEEDBACK_D, "metadata": (["type": "core", "description": "物品状态反馈/同步辅助服务", "dependencies": ({"item", "client_projection", "client_sync"})])]),
        "combat": (["path": COMBAT_D, "metadata": (["type": "core", "description": "战斗服务", "dependencies": ({"player"})])]),
        "combat_damage": (["path": COMBAT_DAMAGE_D, "metadata": (["type": "core", "description": "战斗伤害多核 proposal 服务", "dependencies": ({"combat", "proposal_commit", "worker_bus"})])]),
        "chat": (["path": CHAT_D, "metadata": (["type": "core", "description": "聊天服务", "dependencies": ({"player"})])])
    ]);

    register_service_catalog(services);
}

void register_system_services()
{
    mapping services;

    services = ([
        "registry": (["path": REGISTRY_SERVICE, "metadata": (["type": "system", "description": "服务注册表", "dependencies": ({})])]),
        "config": (["path": CONFIG_D, "metadata": (["type": "system", "description": "配置服务", "dependencies": ({})])]),
        "content_registry": (["path": CONTENT_REGISTRY_D, "metadata": (["type": "system",
        "description": "内容注册与所有权校验服务", "dependencies": ({"config"})])]),
        "world_state": (["path": WORLD_STATE_D, "metadata": (["type": "system",
        "description": "共享世界状态代数与持久发布服务", "dependencies": ({"config"})])]),
        "world_context": (["path": WORLD_CONTEXT_D, "metadata": (["type": "system",
        "description": "共享世界上下文与玩家投影边界", "dependencies": ({"config", "world_state"})])]),
        "time": (["path": TIME_D, "metadata": (["type": "system", "description": "世界时间快照服务",
        "dependencies": ({"config"})])]),
        "world_event": ([
            "path": WORLD_EVENT_D,
            "metadata": ([
                "type": "system",
                "description": "世界事件持久激活与恢复服务",
                "dependencies": ({"content_registry", "world_state", "time"})
            ])
        ]),
        "npc_schedule": ([
            "path": NPC_SCHEDULE_D,
            "metadata": ([
                "type": "system",
                "description": "NPC 稳定槽位日程服务",
                "dependencies": ({"content_registry", "world_state", "time"})
            ])
        ]),
        "instance_room": ([
            "path": INSTANCE_ROOM_D,
            "metadata": ([
                "type": "system",
                "description": "通用实例房间生命周期服务",
                "dependencies": ({"config"})
            ])
        ]),
        "login_projection": ([
            "path": LOGIN_PROJECTION_D,
            "metadata": ([
                "type": "system",
                "description": "连接期投影身份与登录位置恢复服务",
                "dependencies": ({"world_travel", "instance_room", "content_registry",
                    "client_sync", "config"})
            ])
        ]),
        "world_travel": ([
            "path": WORLD_TRAVEL_D,
            "metadata": ([
                "type": "system",
                "description": "实例感知的统一玩家移动边界",
                "dependencies": ({"instance_room", "dungeon", "config"})
            ])
        ]),
        "world_lifecycle": ([
            "path": WORLD_LIFECYCLE_D,
            "metadata": ([
                "type": "system",
                "description": "世界生命周期兼容 facade",
                "dependencies": ({"world_entity_lifecycle", "world_projection",
                    "world_observer"})
            ])
        ]),
        "world_room": (["path": WORLD_ENTITY_LIFECYCLE_D, "metadata": (["type": "system",
        "description": "世界房间实体 owner 路由", "dependencies": ({"world_entity_lifecycle"})])]),
        "world_entity_lifecycle": ([
            "path": WORLD_ENTITY_LIFECYCLE_D,
            "metadata": ([
                "type": "system",
                "description": "动态实体槽位与租约服务",
                "dependencies": ({"content_registry", "instance_room", "world_event",
                    "npc_schedule", "owner_runtime"})
            ])
        ]),
        "world_projection": ([
            "path": WORLD_PROJECTION_D,
            "metadata": ([
                "type": "system",
                "description": "世界投影与实体可见性服务",
                "dependencies": ({"config", "world_context", "world_state",
                    "client_projection", "world_observer", "world_entity_lifecycle"})
            ])
        ]),
        "world_observer": ([
            "path": WORLD_OBSERVER_D,
            "metadata": ([
                "type": "system",
                "description": "房间观察与刷新计数服务",
                "dependencies": ({"world_entity_lifecycle", "room_view"})
            ])
        ]),
        "world_orchestrator": ([
            "path": WORLD_ORCHESTRATOR_D,
            "metadata": ([
                "type": "system",
                "description": "世界事件与生命周期编排服务",
                "dependencies": ({"world_event", "world_projection", "world_observer",
                    "world_context", "world_state", "world_travel", "spawn", "story"})
            ])
        ]),
        "content_mode_policy": (["path": CONTENT_MODE_POLICY_D, "metadata": (["type": "system", "description": "内容策略编译与投影服务", "dependencies": ({"entitlement"})])]),
        "log": (["path": LOG_D, "metadata": (["type": "system", "description": "日志服务", "dependencies": ({"config"})])]),
        "command_dispatch": (["path": COMMAND_DISPATCH_D, "metadata": (["type": "system", "description": "命令分发服务", "dependencies": ({"config"})])]),
        "login": ([
            "path": "/kernel/login",
            "metadata": ([
                "type": "system",
                "description": "登录入口 owner 路由",
                "dependencies": ({"gateway_session", "owner_runtime"}),
                "health_check": "health_check"
            ])
        ]),
        "player_session": ([
            "path": "/lib/base/player_session",
            "metadata": ([
                "type": "system",
                "description": "玩家连接会话 owner 路由",
                "dependencies": ({"gateway_session", "owner_runtime", "client_sync"}),
                "health_check": "health_check"
            ])
        ]),
        "heartbeat": (["path": HEARTBEAT_D, "metadata": (["type": "system",
        "description": "心跳调度 owner 路由", "dependencies": ({"owner_runtime"})])]),
        "delayed_callback": (["path": TIMER_D, "metadata": (["type": "system",
        "description": "延迟回调 owner 路由", "dependencies": ({"owner_runtime"})])]),
        "command_owner_txn": (["path": COMMAND_OWNER_TXN_D, "metadata": (["type": "system",
        "description": "owner 命令事务边界", "dependencies": ({"owner_commit", "owner_persistence"})])]),
        "client_sync": (["path": CLIENT_SYNC_D, "metadata": (["type": "system",
        "description": "客户端状态同步协调服务", "dependencies": ({}), "health_check": "health_check"])]),
        "client_projection": (["path": CLIENT_PROJECTION_D, "metadata": (["type": "system", "description": "客户端协议投影服务", "dependencies": ({"item"})])]),
        "room_view": (["path": ROOM_VIEW_D, "metadata": (["type": "system", "description": "房间视图投影服务", "dependencies": ({"quest_actor"})])]),
        "dialog_projection": (["path": DIALOG_PROJECTION_D, "metadata": (["type": "system", "description": "对话协议投影服务", "dependencies": ({})])]),
        "data_maintenance": (["path": DATA_MAINTENANCE_D, "metadata": (["type": "system", "description": "运行时数据维护服务", "dependencies": ({"log"})])]),
        "migration": ([
            "path": "/daemons/services/system/migration_d",
            "metadata": ([
                "type": "system",
                "description": "账号与角色一致性迁移服务",
                "dependencies": ({"player"}),
                "health_check": "health_check"
            ])
        ]),
        "shutdown": (["path": SHUTDOWN_D, "metadata": (["type": "system", "description": "服务器关闭服务", "dependencies": ({"log"})])]),
        "health": (["path": HEALTH_SERVICE, "metadata": (["type": "system", "description": "健康检查服务", "dependencies": ({"registry", "log"})])]),
        "name": (["path": NAME_D, "metadata": (["type": "system", "description": "命名服务", "dependencies": ({})])]),
        "security": (["path": SECURITY_MONITOR_D, "metadata": (["type": "system", "description": "安全监控服务", "dependencies": ({"player", "log"})])]),
        "monitor": (["path": MONITOR_D, "metadata": (["type": "system", "description": "监控服务", "dependencies": ({"log"})])]),
        "worker_bus": ([
            "path": WORKER_BUS_D,
            "metadata": ([
                "type": "system",
                "description": "外围 worker 快照任务门面服务",
                "dependencies": ({}),
                "health_check": "health_check"
            ])
        ]),
        "multicore_config": (["path": MULTICORE_CONFIG_D, "metadata": (["type": "system", "description": "多核执行模式与灰度开关真源", "dependencies": ({})])]),
        "async_compute": (["path": ASYNC_COMPUTE_D, "metadata": (["type": "system", "description": "统一多核异步计算服务", "dependencies": ({"worker_bus"})])]),
        "proposal_commit": (["path": PROPOSAL_COMMIT_D, "metadata": (["type": "system", "description": "多核 proposal 主线程提交守门服务", "dependencies": ({"async_compute"})])]),
        "read_summary": (["path": READ_SUMMARY_D, "metadata": (["type": "system", "description": "高频读侧摘要异步计算服务", "dependencies": ({"async_compute"})])]),
        "owner_route": ([
            "path": OWNER_ROUTE_D,
            "metadata": ([
                "type": "system",
                "description": "owner 路由与 actor 快照真源",
                "dependencies": ({"gateway_session", "worker_bus"}),
                "health_check": "health_check"
            ])
        ]),
        "owner_runtime": ([
            "path": OWNER_RUNTIME_D,
            "metadata": ([
                "type": "system",
                "description": "mudlib owner runtime 统一门面",
                "dependencies": ({"owner_route"}),
                "health_check": "health_check"
            ])
        ]),
        "owner_message_bus": (["path": OWNER_MESSAGE_BUS_D, "metadata": (["type": "system", "description": "跨 owner 消息总线门面", "dependencies": ({"owner_runtime"})])]),
        "owner_commit": (["path": OWNER_COMMIT_D, "metadata": (["type": "system", "description": "跨 owner 提交边界协调器", "dependencies": ({"owner_message_bus"})])]),
        "player_state": (["path": PLAYER_STATE_D, "metadata": (["type": "system", "description": "玩家状态 schema/CAS/快照 owner", "dependencies": ({"owner_commit"})])]),
        "owner_session_fifo": (["path": OWNER_SESSION_FIFO_D, "metadata": (["type": "system", "description": "owner 输出 session FIFO 边界", "dependencies": ({"gateway_session", "owner_runtime"})])]),
        "owner_persistence": (["path": OWNER_PERSISTENCE_D, "metadata": (["type": "system", "description": "owner snapshot、持久收据日志与重试门面", "dependencies": ({"owner_runtime", "owner_commit"}), "health_check": "health_check"])]),
        "owner_gameplay": (["path": OWNER_GAMEPLAY_D, "metadata": (["type": "system", "description": "全游戏 owner runtime 迁移协调器", "dependencies": ({"owner_runtime", "owner_message_bus", "owner_commit", "owner_session_fifo", "owner_persistence"})])]),
        "gateway_transport": (["path": XK_TRANSPORT_D, "metadata": (["type": "system", "description": "XK direct/driver fallback capability marker; delivery owner is protocol core", "dependencies": ({})])]),
        "gateway_session": (["path": GATEWAY_SESSION_D, "metadata": (["type": "system",
        "description": "Go 网关逻辑会话真源", "dependencies": ({}), "health_check": "health_check"])])
    ]);

    register_service_catalog(services);
}

void register_game_services()
{
    mapping services;

    services = ([
        "money": (["path": MONEY_D, "metadata": (["type": "game", "description": "货币服务", "dependencies": ({"player"})])]),
        "economy_settlement": (["path": ECONOMY_SETTLEMENT_D, "metadata": (["type": "game",
        "description": "经济结算与补偿服务", "dependencies": ({"player", "money", "owner_commit"})])]),
        "bank": (["path": BANK_D, "metadata": (["type": "game", "description": "银行服务", "dependencies": ({"player", "money"})])]),
        "economy_feedback": (["path": ECONOMY_FEEDBACK_D, "metadata": (["type": "game", "description": "经济反馈/刷新辅助服务", "dependencies": ({"bank", "money", "client_projection", "client_sync"})])]),
        "shop": (["path": SHOP_D, "metadata": (["type": "game", "description": "商店服务", "dependencies": ({"item", "money"})])]),
        "skill": (["path": SKILL_D, "metadata": (["type": "game", "description": "技能服务", "dependencies": ({"player"})])]),
        "numeric_rule": (["path": NUMERIC_RULE_D, "metadata": (["type": "game", "description": "底层数值规则服务", "dependencies": ({"config", "skill"})])]),
        "skill_source_registry": (["path": SKILL_SOURCE_REGISTRY_D, "metadata": (["type": "game", "description": "技能来源注册表", "dependencies": ({"config"})])]),
        "skill_source": (["path": SKILL_SOURCE_D, "metadata": (["type": "game", "description": "技能来源服务", "dependencies": ({"skill", "skill_source_registry"})])]),
        "skill_unlock_registry": (["path": SKILL_UNLOCK_REGISTRY_D, "metadata": (["type": "game", "description": "技能解锁注册表", "dependencies": ({"config"})])]),
        "skill_unlock": (["path": SKILL_UNLOCK_D, "metadata": (["type": "game", "description": "技能解锁服务", "dependencies": ({"skill", "skill_unlock_registry"})])]),
        "skill_behavior": (["path": SKILL_BEHAVIOR_D, "metadata": (["type": "game", "description": "技能运行行为服务", "dependencies": ({"skill", "numeric_rule"})])]),
        "skill_breakthrough": (["path": SKILL_BREAKTHROUGH_D, "metadata": (["type": "game", "description": "技能突破选择状态服务", "dependencies": ({"skill", "numeric_rule"})])]),
        "life_skill": (["path": LIFE_SKILL_D, "metadata": (["type": "game", "description": "生活技能服务", "dependencies": ({"player"})])]),
        "quest": (["path": QUEST_D, "metadata": (["type": "game", "description": "任务服务", "dependencies": ({"player"})])]),
        "quest_progress": (["path": QUEST_PROGRESS_D, "metadata": (["type": "game", "description": "任务进度/目标判定服务", "dependencies": ({"quest"})])]),
        "quest_transition": (["path": QUEST_TRANSITION_D, "metadata": (["type": "game", "description": "任务阶段迁移驱动服务", "dependencies": ({"quest"})])]),
        "quest_settlement": (["path": QUEST_SETTLEMENT_D, "metadata": (["type": "game", "description": "任务结算与奖励驱动服务", "dependencies": ({"quest", "reward_apply", "reward_feedback"})])]),
        "quest_reward": (["path": QUEST_SETTLEMENT_D, "metadata": (["type": "game",
        "description": "任务奖励 owner 路由别名", "dependencies": ({"quest_settlement"})])]),
        "dungeon": (["path": DUNGEON_D, "metadata": (["type": "game", "description": "副本服务", "dependencies": ({"combat", "player"})])]),
        "mail": (["path": MAIL_D, "metadata": (["type": "game", "description": "邮件服务", "dependencies": ({"player"})])]),
        "guild": (["path": GUILD_D, "metadata": (["type": "game", "description": "帮派服务", "dependencies": ({"player"})])]),
        "team": (["path": TEAM_D, "metadata": (["type": "game", "description": "队伍服务", "dependencies": ({"player"})])]),
        "social": (["path": SOCIAL_SERVICE, "metadata": (["type": "game", "description": "社交子服务加载与队伍聚合服务", "dependencies": ({"chat", "guild", "mail", "team"})])]),
        "rank": (["path": RANK_D, "metadata": (["type": "game", "description": "排行榜服务", "dependencies": ({"player"})])]),
        "player_actor_analysis": (["path": PLAYER_ACTOR_ANALYSIS_D, "metadata": (["type": "game", "description": "玩家读侧多核分析服务", "dependencies": ({"owner_route", "async_compute"})])]),
        "region": (["path": REGION_D, "metadata": (["type": "game", "description": "区域元数据门面服务", "dependencies": ({"config"})])]),
        "spawn": (["path": SPAWN_D, "metadata": (["type": "game", "description": "刷新元数据/事件门面服务", "dependencies": ({"region", "config"})])]),
        "story": (["path": STORY_D, "metadata": (["type": "game", "description": "剧情触发门面服务", "dependencies": ({"region", "quest"})])]),
        "room_behavior": (["path": ROOM_BEHAVIOR_D, "metadata": (["type": "game", "description": "房间行为命令分发服务", "dependencies": ({"config"})])]),
        "npc_behavior": (["path": NPC_BEHAVIOR_D, "metadata": (["type": "game", "description": "NPC 原型元数据服务", "dependencies": ({"config"})])]),
        "dialog": (["path": DIALOG_D, "metadata": (["type": "game", "description": "NPC 对话模板门面服务", "dependencies": ({})])]),
        "npc_view": (["path": NPC_VIEW_D, "metadata": (["type": "game", "description": "NPC 按玩家任务视图解析服务", "dependencies": ({"dialog", "quest", "quest_scene"})])]),
        "quest_actor": ([
            "path": QUEST_ACTOR_D,
            "metadata": ([
                "type": "game",
                "description": "任务演员稳定身份与投影服务",
                "dependencies": ({"npc_view", "quest", "world_state", "content_registry",
                    "world_entity_lifecycle", "client_projection"})
            ])
        ]),
        "quest_scene": (["path": QUEST_SCENE_D, "metadata": (["type": "game", "description": "任务阶段剧情驱动服务", "dependencies": ({"quest"})])])
    ]);

    register_service_catalog(services);
}

void auto_register_services()
{
    register_core_services();
    register_system_services();
    register_game_services();

    if (file_size(HEALTH_SERVICE + ".c") > 0)
        HEALTH_SERVICE->start_health_check();
}

private void rebuild_dependency_graph()
{
    mapping nodes;
    mapping edges;
    mapping reverse;
    mapping indegree;
    mapping missing;
    mapping node;
    mapping metadata;
    string *queue;
    string *order;
    string *cycle_nodes;
    string *dependencies;
    string name;
    string dependency;
    int index;
    int dependency_ready;

    nodes = ([]);
    edges = ([]);
    reverse = ([]);
    indegree = ([]);
    missing = ([]);
    queue = ({ });
    order = ({ });
    cycle_nodes = ({ });

    foreach (name in keys(_services)) {
        metadata = mapp(_service_metadata[name]) ? _service_metadata[name] : ([]);
        dependencies = arrayp(metadata["dependencies"]) ? metadata["dependencies"] : ({ });
        nodes[name] = ([
            "service": name,
            "path": _services[name]["path"],
            "status": _services[name]["status"],
            "dependencies": ([ ]),
            "missing_dependencies": ({ }),
            "ready": 0,
        ]);
        edges[name] = dependencies;
        indegree[name] = 0;
    }

    foreach (name, dependencies in edges) {
        foreach (dependency in dependencies) {
            if (!mapp(nodes[dependency])) {
                if (!arrayp(missing[name]))
                    missing[name] = ({ });
                missing[name] += ({ dependency });
                nodes[name]["missing_dependencies"] += ({ dependency });
                continue;
            }
            indegree[name]++;
            if (!arrayp(reverse[dependency]))
                reverse[dependency] = ({ });
            reverse[dependency] += ({ name });
        }
    }

    foreach (name in keys(nodes))
        if (indegree[name] == 0)
            queue += ({ name });

    index = 0;
    while (index < sizeof(queue)) {
        name = queue[index++];
        order += ({ name });
        foreach (string dependent in reverse[name] || ({ })) {
            indegree[dependent]--;
            if (indegree[dependent] == 0)
                queue += ({ dependent });
        }
    }

    foreach (name in keys(nodes))
        if (member_array(name, order) == -1)
            cycle_nodes += ({ name });

    foreach (name in cycle_nodes) {
        foreach (dependency in edges[name]) {
            nodes[name]["dependencies"][dependency] = ([
                "registered": mapp(nodes[dependency]) ? 1 : 0,
                "ready": 0,
            ]);
        }
    }

    foreach (name in order) {
        dependency_ready = 1;
        foreach (dependency in edges[name]) {
            if (!mapp(nodes[dependency])) {
                dependency_ready = 0;
                nodes[name]["dependencies"][dependency] = ([
                    "registered": 0,
                    "ready": 0,
                ]);
            } else {
                nodes[name]["dependencies"][dependency] = ([
                    "registered": 1,
                    "ready": nodes[dependency]["ready"],
                ]);
                if (nodes[dependency]["ready"] != 1)
                    dependency_ready = 0;
            }
        }
        nodes[name]["ready"] = sizeof(nodes[name]["missing_dependencies"]) == 0 &&
            member_array(name, cycle_nodes) == -1 && dependency_ready &&
            _services[name]["status"] == SERVICE_ACTIVE ? 1 : 0;
    }

    _dependency_graph = ([
        "ok": sizeof(keys(missing)) == 0 && sizeof(cycle_nodes) == 0 ? 1 : 0,
        "nodes": nodes,
        "edges": edges,
        "missing_dependencies": missing,
        "cycles": cycle_nodes,
        "topological_order": order,
        "generated_at": time(),
    ]);
}

mapping query_dependency_graph()
{
    return restore_variable(save_variable(_dependency_graph || ([ ])));
}

mapping query_service_readiness(string name)
{
    mapping graph;
    mapping node;
    mapping dependencies;
    mixed missing_dependencies;
    int graph_ok;
    int node_ready;

    /* Route checks only need one node. Do not serialize the complete graph on
     * every room/player hot-path query: the full snapshot is intentionally
     * reserved for diagnostics through query_dependency_graph(). */
    graph = mapp(_dependency_graph) ? _dependency_graph : ([ ]);
    graph_ok = graph["ok"] == 1 ? 1 : 0;
    node = mapp(graph["nodes"]) ? graph["nodes"][name] : 0;
    if (!mapp(node))
        return ([
            "ok": 0,
            "graph_ok": graph_ok,
            "registered": 0,
            "node_ready": 0,
            "service": name,
            "dependencies": ([ ]),
            "missing_dependencies": ({ }),
            "failure_reason": "service is not registered",
        ]);

    dependencies = restore_variable(save_variable(
        mapp(node["dependencies"]) ? node["dependencies"] : ([ ])));
    missing_dependencies = arrayp(node["missing_dependencies"])
        ? node["missing_dependencies"] + ({ }) : ({ });
    node_ready = node["ready"] == 1 ? 1 : 0;
    return ([
        "ok": node_ready && graph_ok ? 1 : 0,
        "graph_ok": graph_ok,
        "registered": 1,
        "node_ready": node_ready,
        "service": name,
        "dependencies": mapp(dependencies) ? dependencies : ([ ]),
        "missing_dependencies": missing_dependencies,
        "failure_reason": node_ready && graph_ok
            ? "" : "service dependency graph is not ready",
    ]);
}

/* 注册路径就是服务对象路径，随后会被 get_service()/query_service() 动态解析。
 * 公共服务宏使用不带 .c 的对象路径，因此校验时补到源文件再检查；只允许
 * daemon/kernel/lib 三个代码树，拒绝相对路径、目录穿越和 world 内容路径。 */
private int is_registrable_service_path(string path)
{
    string source_path;

    if (!stringp(path) || strlen(path) < 2 || path[0] != '/')
        return 0;
    if (strsrch(path, "..") >= 0 || strsrch(path, "//") >= 0)
        return 0;
    if (strsrch(path, "/daemons/") != 0
        && strsrch(path, "/kernel/") != 0
        && strsrch(path, "/lib/") != 0)
        return 0;
    if (path[strlen(path) - 2..] == ".c")
        source_path = path;
    else {
        if (strsrch(path, ".") >= 0)
            return 0;
        source_path = path + ".c";
    }
    return file_size(source_path) > 0;
}

void register_service(string name, string path, mapping metadata)
{
    string health_check;

    if (!stringp(name) || !is_registrable_service_path(path))
        return;

    _services[name] = ([
        "path": path,
        "status": SERVICE_ACTIVE,
        "registered_at": time()
    ]);
    _service_metadata[name] = metadata || ([]);
    _service_stats[name] = ([
        "call_count": 0,
        "error_count": 0,
        "last_called": 0,
        "avg_response_time": 0
    ]);

    if (file_size(HEALTH_SERVICE + ".c") > 0) {
        health_check = mapp(metadata) ? metadata["health_check"] : 0;
        HEALTH_SERVICE->register_service(name, health_check);
    }
    rebuild_dependency_graph();
}

void unregister_service(string name)
{
    if (!stringp(name))
        return;
    map_delete(_services, name);
    map_delete(_service_metadata, name);
    map_delete(_service_stats, name);

    if (file_size(HEALTH_SERVICE + ".c") > 0)
        HEALTH_SERVICE->unregister_service(name);
    rebuild_dependency_graph();
}

string discover_service(string name)
{
    mapping service;

    /*
     * discover_service 不只是“查路径”，它同时承担轻量调用统计。
     * 真正的服务发现发生在这里，get_service 只是把结果转成对象。
     */
    if (!stringp(name) || !mapp(_services[name]))
        return 0;
    service = _services[name];
    if (service["status"] != SERVICE_ACTIVE)
        return 0;
    _service_stats[name]["call_count"]++;
    _service_stats[name]["last_called"] = time();
    return service["path"];
}

object get_service(string name)
{
    string path;
    object svc;

    /*
     * registry 只关心“服务名 -> 路径 -> 对象”的解析与加载，
     * 不负责服务内部初始化细节。
     */
    path = discover_service(name);
    if (!stringp(path))
        return 0;
    svc = find_object(path);
    if (!objectp(svc))
        svc = load_object(path);
    return svc;
}

void update_service_status(string name, string status)
{
    if (!stringp(name) || !mapp(_services[name]))
        return;
    if (member_array(status, ({ SERVICE_ACTIVE, SERVICE_INACTIVE, SERVICE_DEGRADED, SERVICE_FAILED })) == -1)
        return;
    _services[name]["status"] = status;
    _services[name]["status_changed_at"] = time();
    rebuild_dependency_graph();
}

mapping query_service(string name)
{
    mapping result;
    string path;

    if (!stringp(name) || !mapp(_services[name]))
        return 0;
    result = copy(_services[name]);
    result["metadata"] = copy(_service_metadata[name] || ([]));
    result["stats"] = copy(_service_stats[name] || ([]));
    path = result["path"];
    if (stringp(path))
        result["object_exists"] = objectp(find_object(path));
    return result;
}

mapping query_all_services()
{
    mapping result;
    string name;

    result = ([]);
    foreach (name in keys(_services))
        result[name] = query_service(name);
    return result;
}

mapping query_services_by_type(string type)
{
    mapping result;
    string name;
    mapping meta;

    result = ([]);
    foreach (name, meta in _service_metadata) {
        if (mapp(meta) && meta["type"] == type)
            result[name] = query_service(name);
    }
    return result;
}

string *list_services()
{
    return keys(_services || ([]));
}
