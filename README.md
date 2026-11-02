# 侠客行 · XiaKeXing

> 中文武侠题材在线角色扮演游戏的公开展示快照。

**English summary:** XiaKeXing is a Chinese wuxia online RPG project. This repository is a curated public showcase of its visual direction and product surface. The private implementation, service runtime, and ongoing rewrite are intentionally not published here.

## 这个仓库是什么

这个仓库从私有开发仓库的 `main` 版本整理而来，目标是让求职、作品展示和技术交流可以直接看到项目的产品形态，而不是公开完整的运行系统。

公开内容聚焦于：

- 武侠题材的中文界面与视觉方向；
- Android 客户端的屏幕结构和交互结果；
- 房间导航、战斗、背包、技能和地图等主要产品场景；
- 一个可以独立阅读的项目说明和静态画面证据。

## 画面展示

以下画面来自本地 UI 基线截图。它们用于展示设计和交互方向，不代表当前线上服务，也不提供可登录的公共账号。

<table>
  <tr>
    <td><img src="showcase/screenshots/main.png" alt="游戏主界面" width="260" /></td>
    <td><img src="showcase/screenshots/combat.png" alt="战斗界面" width="260" /></td>
  </tr>
  <tr>
    <td align="center">主界面与房间导航</td>
    <td align="center">战斗状态与叙事区域</td>
  </tr>
  <tr>
    <td><img src="showcase/screenshots/inventory.png" alt="背包界面" width="260" /></td>
    <td><img src="showcase/screenshots/skills.png" alt="技能界面" width="260" /></td>
  </tr>
  <tr>
    <td align="center">背包与物品分类</td>
    <td align="center">技能面板与角色成长</td>
  </tr>
  <tr>
    <td colspan="2" align="center"><img src="showcase/screenshots/map.png" alt="地图界面" width="360" /></td>
  </tr>
  <tr>
    <td colspan="2" align="center">地图浏览与空间导航</td>
  </tr>
</table>

## 产品方向

侠客行希望把传统文字冒险中的信息密度，与移动端更容易理解的结构化界面结合起来。当前展示重点包括：

1. **以场景为中心**：当前位置、可移动出口、场景对象和叙事文本保持在同一工作区内。
2. **可读的战斗反馈**：角色和对手的状态使用稳定的视觉层级呈现，减少长文本中的信息丢失。
3. **统一的面板语言**：背包、技能、任务、地图和交互面板使用同一套深色水墨视觉基线。
4. **中文内容优先**：界面、物品、技能和场景文本围绕中文武侠语境设计。
5. **移动端操作优先**：主要动作保持清晰的触控区域和可预期的面板层级。

## 技术方向

私有开发版本的客户端方向包括 Android、Kotlin 和 Jetpack Compose。公开仓库只保留可展示的产品结果，不公开完整客户端源码或服务端实现，因此本仓库**不是一个可以直接构建和运行游戏的源码发行版**。

这里展示的是最终可观察的界面与产品边界；账号系统、网络连接、协议、持久化、服务端运行时和内容生产链路均不在公开范围内。

## 公开边界

为了保护仍在持续开发的私有重写版本，本仓库明确不包含以下内容：

- 服务端、Driver、Gateway 和运行时实现；
- 登录凭据、账号数据、运行数据、生产配置和部署脚本；
- 网络协议、认证、持久化和业务提交的内部实现；
- 产品合同、架构规则、迁移计划、内部决策和测试报告；
- 私有开发分支、标签、提交历史和未发布构件；
- 公共服务地址、测试账号和可直接使用的登录入口。

这些内容继续保留在私有开发仓库中。这个公开仓库不是私有仓库的镜像，也不会自动同步私有分支。

## 如何查看

直接打开本页即可查看项目说明和画面。截图位于 [`showcase/screenshots/`](showcase/screenshots/)。

本仓库没有公共服务端、公共测试账号或可下载的游戏构件；不要根据截图尝试连接未公开的运行实例。

## 项目状态

项目仍在私有环境中持续开发。公开仓库只在经过人工审查后发布新的展示快照，公开内容与私有实现之间不保证逐提交对应。

## 许可

本仓库沿用 [`LICENSE`](LICENSE) 中的 PolyForm Noncommercial License 1.0.0。第三方资源如有单独许可，以其原始许可文本为准。
