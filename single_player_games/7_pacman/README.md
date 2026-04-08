# Pacman

![Pacman](./7_pacman.jpg)

文件: [7_pacman.ino](./7_pacman.ino)

## 控制
- 标题页蓝键: 开始
- 游戏中摇杆: 移动
- 游戏中绿键: 暂停 / 继续
- 结算页蓝键: 重开
- 结算页绿键: 回标题

## 玩法
- 吃豆加分，吃完豆子进入下一关。
- 吃大力丸后可反吃幽灵。
- 生命耗尽进入排行榜。

## 排行榜
- 大小 `5`，初始分数全 `0`。
- 当前成绩进入榜单时高亮。

## 可调参数（含代码行）
- 逻辑节拍 `GAME_UPDATE_INTERVAL_MS`（46）
- 玩家/幽灵速度 `PLAYER_SPEED_PX` / `GHOST_SPEED_PX`（47-48）
- 受惊/回家速度 `FRIGHTENED_SPEED_PX` / `EATEN_GHOST_SPEED_PX`（49-50）
- 大力丸时长 `POWER_MODE_MS`（51）
- 掉命与过关延迟 `RESPAWN_DELAY_MS` / `LEVEL_CLEAR_DELAY_MS`（52-53）
- 初始生命 `PLAYER_LIVES`（54）
- 排行榜大小 `LEADERBOARD_SIZE`（56）
- 迷宫模板 `MAZE_TEMPLATE`（129-145）
- 排行榜初始值 `leaderboard[LEADERBOARD_SIZE] = {0, 0, 0, 0, 0}`（171）

<div align="center">
  <a href="../../README.md" style="display: inline-block; margin: 10px 0 18px; padding: 10px 18px; border-radius: 999px; background: linear-gradient(135deg, #1f6feb, #3fb950); color: #ffffff; text-decoration: none; font-weight: 700; box-shadow: 0 4px 12px rgba(31, 111, 235, 0.25);">返回 README 主页</a>
</div>
