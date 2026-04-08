# Tetris (ESP32)

![Tetris (ESP32)](./2_tetris.jpg)

文件: [2_tetris.ino](./2_tetris.ino)

## 控制
- 摇杆左/右: 平移方块
- 摇杆下: 软降
- 绿键: 旋转
- 蓝键: 开始 / 暂停 / 继续 / 重开

## 规则
- 默认累计消除 `40` 行通关。
- 消行触发音效与震动，失败与通关播放结尾旋律。

## 可调参数（含代码行）
- 棋盘尺寸 `BOARD_COLS` / `BOARD_ROWS` / `CELL`（34-36）
- 棋盘与 HUD 位置 `BOARD_X` / `BOARD_Y` / `HUD_X`（37-39）
- 通关行数 `WIN_LINE_TARGET`（91）
- 软降触发节奏 `now - lastDownMs >= 55`（656）
- 左右移动节奏 `now - lastMoveMs >= 120`（639, 642）
- 锁定延迟 `now - lockDelayStartMs >= 260`（688）
- 下落速度公式 `base = 720 - (level - 1) * 55`（538）
- 最低下落间隔 `base < 110`（539）
- 消行震动时长 `120 + lines * 50`（602）

<div align="center">
  <a href="../../README.md" style="display: inline-block; margin: 10px 0 18px; padding: 10px 18px; border-radius: 999px; background: linear-gradient(135deg, #1f6feb, #3fb950); color: #ffffff; text-decoration: none; font-weight: 700; box-shadow: 0 4px 12px rgba(31, 111, 235, 0.25);">返回 README 主页</a>
</div>
