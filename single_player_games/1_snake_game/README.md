# Snake Game

![Snake Game](./1_snake_game.jpg)

文件: [1_snake_game.ino](./1_snake_game.ino)

## 玩法
- 菜单按按钮开始。
- 摇杆控制蛇移动，吃到食物加分并加速。
- 撞墙或撞到自己后结束，显示本局分数和最高分。
- 按钮可暂停/继续，结束后按按钮回菜单并可重开。

## 控制
- 摇杆: 方向控制
- 按钮: 开始 / 暂停 / 继续 / 结束后返回
- LED `47`: 菜单/暂停/结束闪烁，游戏中常亮

## 可调参数（含代码行）
- `SCREEN_WIDTH` / `SCREEN_HEIGHT`（9-10）
- `GRID_SIZE` / `GAME_WIDTH` / `GAME_HEIGHT`（11-13）
- `MAX_SNAKE_LENGTH`（14）
- `JOYSTICK_CENTER` / `JOYSTICK_THRESHOLD`（16-17）
- `GAME_SPEED_INIT` / `GAME_SPEED_MIN`（19-20）
- 初始长度 `snakeLength = 3`（64）
- 单次得分 `score += 10`（241）
- 吃到食物后的加速幅度 `gameSpeed -= 5`（245）
- 顶部 HUD 高度 `drawRect(0, 40, ...)`（286）

<div align="center">
  <a href="../../README.md" style="display: inline-block; margin: 10px 0 18px; padding: 10px 18px; border-radius: 999px; background: linear-gradient(135deg, #1f6feb, #3fb950); color: #ffffff; text-decoration: none; font-weight: 700; box-shadow: 0 4px 12px rgba(31, 111, 235, 0.25);">返回 README 主页</a>
</div>
