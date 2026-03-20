# RAD Runner

![RAD Runner](./5_rad_runner.jpg)

文件: `5_rad_runner.ino`

## 玩法
- 摇杆左右控制车辆，不断前进累计里程。
- 碰撞后进入爆炸与结算，再进入排行榜。
- 排行榜初始为 5 条 `0.0 km`，当前成绩会高亮。

## 可调参数（含代码行）
- 帧间隔 `FRAME_MS = 33`（12）
- 基础车速 `BASE_SPEED = 1.15f`（14）
- 速度增长 `BASE_SPEED + min(1.55f, distanceMeters / 3200.0f)`（453）
- 横向限制 `constrain(playerOffset, -0.82f, 0.82f)`（464）
- 弯道随机幅度 `random(-150, 151) / 100.0f`（458）
- 同屏车数 `2 + min(3, (int)(distanceMeters / 3200.0f))`（473）
- 密度增长 `min(0.028f, distanceMeters / 220000.0f)`（476）
- 生成深度 `min(0.24f, 0.10f + distanceMeters / 50000.0f)`（480）
- 碰撞判定高度 `rect.y > SCREEN_H - 96`（489）
- 爆炸帧速 `explosionFrame ... / 90`（761）
- 爆炸停留 `millis() - explosionStartMs > 1100`（762）
- 震动强度/节奏 `duty = 160`、`>= 35`（224, 221）
- 排行榜大小 `LEADERBOARD_SIZE = 5`（13）
- 当前成绩高亮色 `incoming.color`（402）

<div align="center">
  <a href="../../README.md" style="display: inline-block; margin: 10px 0 18px; padding: 10px 18px; border-radius: 999px; background: linear-gradient(135deg, #1f6feb, #3fb950); color: #ffffff; text-decoration: none; font-weight: 700; box-shadow: 0 4px 12px rgba(31, 111, 235, 0.25);">返回 README 主页</a>
</div>
