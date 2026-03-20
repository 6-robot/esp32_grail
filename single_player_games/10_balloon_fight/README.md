# Balloon Fight

![Balloon Fight](./10_balloon_fight.jpg)

文件: `10_balloon_fight.ino`

## 控制
- 摇杆左右: 漂移
- 摇杆上推或绿键: 扇翅上升
- 蓝键: 标题开始 / 结束后重开

## 玩法
- 玩家与 AI 敌人在平台间空战。
- 踩中对方气球会使其气球数减少。
- 气球清零后目标进入坠落状态，落地判负。
- 对局结束显示 `YOU WIN` / `YOU LOSE`。

## 可调参数（含代码行）
- 逻辑步长 `GAME_UPDATE_INTERVAL_MS`（46）
- 重力 `GRAVITY`（40）
- 扇翅冲量 `FLAP_IMPULSE`（41）
- 玩家水平速度上限 `MAX_VX`（42）
- 垂直速度上限 `MAX_VY_UP` / `MAX_VY_DOWN` / `FALL_MAX_VY`（43-45）
- 初始气球数量 `f.balloons = 2`（401）
- 玩家/敌人开局坐标 `resetFighter(...)`（444-445）
- 眩晕与无敌时长 `stunUntil = millis() + 700` / `invulnUntil = millis() + 950`（499-500）
- 被击飞速度 `victim.vx = ... 1.7f`、`victim.vy = -1.8f`（501-502）
- AI 横向速度限制 `clampf(enemy.vx, -2.2f, 2.2f)`（624）

<div align="center">
  <a href="../../README.md" style="display: inline-block; margin: 10px 0 18px; padding: 10px 18px; border-radius: 999px; background: linear-gradient(135deg, #1f6feb, #3fb950); color: #ffffff; text-decoration: none; font-weight: 700; box-shadow: 0 4px 12px rgba(31, 111, 235, 0.25);">返回 README 主页</a>
</div>
