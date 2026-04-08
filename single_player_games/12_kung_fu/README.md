# Kung Fu

![Kung Fu](./12_kung_fu.jpg)

文件: [12_kung_fu.ino](./12_kung_fu.ino)

## 控制
- 蓝键: 标题开始 / 跳跃
- 绿键: 攻击
- 摇杆左右: 移动
- 摇杆下 + 绿键: 低踢
- 空中按绿键: 飞踢

## 玩法
- 横版连续战斗，玩家血量归零后进入排行榜。
- 排行榜目前为内置固定初始值。

## 可调参数（含代码行）
- 主更新步长 `GAME_UPDATE_INTERVAL_MS`（39）
- 玩家血量 `PLAYER_MAX_HP`（40）
- 敌人数上限 `MAX_ENEMIES`（37）
- 玩家初始站位 `player.x = 84.0f` / `player.y = FLOOR_Y`（423-424）
- 移动速度 `player.vx = move * 2.3f`（664）
- 跳跃力度 `player.vy = -5.8f`（680）
- 第一阶段持棍敌人概率 `random(0, 100) < 45`（465）
- 敌人攻击间隔 `random(500, 900)` / `random(760, 1120)`（467）
- 同屏敌人数上限 `activeCount < 2 + min(stageNumber / 2, 1)`（829）
- 阶段推进 `targetStage = 1 + enemiesDefeated / 10`（834）

<div align="center">
  <a href="../../README.md" style="display: inline-block; margin: 10px 0 18px; padding: 10px 18px; border-radius: 999px; background: linear-gradient(135deg, #1f6feb, #3fb950); color: #ffffff; text-decoration: none; font-weight: 700; box-shadow: 0 4px 12px rgba(31, 111, 235, 0.25);">返回 README 主页</a>
</div>
