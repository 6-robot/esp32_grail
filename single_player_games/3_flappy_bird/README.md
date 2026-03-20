# Flappy Bird

![Flappy Bird](./3_flappy_bird.jpg)

文件: `3_flappy_bird.ino`

## 玩法
- 菜单推摇杆上或按按钮进入游戏。
- 游戏中推摇杆上或按按钮拍动。
- 穿过一组管道加 `1` 分。
- 撞管道或落地结束，显示本局分数和最高分。

## 控制
- 摇杆 `Y` 上推: 开始 / 拍动 / 重开
- 按钮: 开始 / 拍动 / 重开
- LED `47`: 菜单、准备、结束闪烁，游戏中常亮

## 可调参数（含代码行）
- 小鸟尺寸与位置 `BIRD_WIDTH` / `BIRD_HEIGHT` / `BIRD_X`（12-14）
- 水管参数 `PIPE_WIDTH` / `PIPE_GAP` / `PIPE_SPEED` / `PIPE_COUNT` / `PIPE_SPACING`（16-20）
- 物理参数 `GRAVITY` / `FLAP_STRENGTH` / `MAX_FALL_SPEED`（22-24）
- 地面高度 `GROUND_HEIGHT`（26）
- 摇杆阈值 `JOYSTICK_CENTER` / `JOYSTICK_THRESHOLD`（28-29）

<div align="center">
  <a href="../../README.md" style="display: inline-block; margin: 10px 0 18px; padding: 10px 18px; border-radius: 999px; background: linear-gradient(135deg, #1f6feb, #3fb950); color: #ffffff; text-decoration: none; font-weight: 700; box-shadow: 0 4px 12px rgba(31, 111, 235, 0.25);">返回 README 主页</a>
</div>
