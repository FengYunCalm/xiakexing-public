# 侠客行 · XiaKeXing

[![公开展示仓库](https://img.shields.io/badge/repository-public-2f855a)](https://github.com/FengYunCalm/xiakexing-public)

侠客行是一款中文文字武侠网络游戏项目，结合 LPC 游戏服务端、Go Gateway 和 Android 客户端。这个仓库用于求职和技术评审，展示 `main` 分支的代表性代码与客户端界面。

> **公开范围**：本仓库只来自私有仓库的 `main` 分支快照，不包含 `rewrite` 分支、`cloud` 分支、私有仓库历史或私有标签。

## 项目结构

```text
client/       Android 客户端生产代码与界面资源
gateway-go/    Go Gateway 生产代码
server/       LPC 服务端运行时、游戏服务和玩家命令代码
showcase/     客户端界面展示图
```

为控制公开边界，以下内容没有发布：

- 服务端配置、管理员配置、内容数据、运行数据和持久化数据；
- 管理员命令、迁移/维护脚本、日志实现和 Driver 适配代码；
- 部署脚本、运维文件、AI 工具、内部规范、设计决策、测试报告和工作计划；
- 测试夹具、测试源文件、完整发布配置、凭据和生产环境参数；
- 私有仓库中的其他分支、标签和历史提交。

因此这是**代码审阅快照**，不是可直接启动线上服务的完整发行包。缺少的配置、Driver、内容和运行数据是有意保留在私有仓库中的边界，不代表项目缺少这些部分。

## 值得查看的部分

### Android 客户端

客户端使用 Kotlin、Jetpack Compose 和模块化结构实现。可以重点查看：

- `client/feature/shell/src/main/`：主游戏界面、面板、交互编排和状态渲染；
- `client/core/`：客户端网络、协议和本地存储模块；
- `client/app/src/main/`：应用入口、主题和窗口控制；
- `showcase/`：当前客户端界面截图。

### Go Gateway

`gateway-go/` 展示 Gateway 的连接管理、帧处理、会话、顺序交付和服务入口代码。部署配置和运行实例没有随代码公开。

### LPC 服务端

`server/` 展示 Driver 之上的游戏运行时、服务对象、玩家命令和游戏服务实现。服务端配置、内容表、管理员入口和持久化数据不在公开快照中。

## 本地查看

只查看客户端代码和截图即可，不需要任何线上账号或服务地址。

如需尝试构建 Android 客户端：

```bash
cd client
./gradlew assembleDebug
```

公开快照没有提供可连接的线上服务，也不接受通过本仓库提交生产配置或凭据。

## 界面展示

| 主界面 | 交互面板 |
| --- | --- |
| ![主界面](showcase/shell-main.png) | ![交互面板](showcase/interaction-sheet.png) |

| 战斗界面 | 任务与世界 |
| --- | --- |
| ![战斗界面](showcase/combat.png) | ![任务与世界](showcase/quest-world.png) |

| 状态与背包 |
| --- |
| ![状态与背包](showcase/status-inventory.png) |

## 技术栈

- **客户端**：Kotlin、Jetpack Compose、Android Gradle Plugin
- **Gateway**：Go
- **游戏服务端**：LPC / FluffOS Driver
- **协议**：客户端、Gateway 与游戏服务端之间的分层消息协议

## 说明

- 当前公开仓库是人工筛选的展示快照，不与私有仓库自动同步。
- 公开代码来自 `main`；后续重写工作保留在私有仓库中。
- 运行数据、账号信息、密钥和生产环境配置不属于本仓库。

## License

见 [`LICENSE`](LICENSE)。
