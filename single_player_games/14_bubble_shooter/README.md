# Bubble Shooter

![Bubble Shooter](./14_bubble_shooter.jpg)

文件: `14_bubble_shooter.ino`

## 控制
- 摇杆左右: 调整发射角度
- 蓝键: 发射
- 绿键: 交换当前球和下一球

## 玩法
- 命中后形成 3 连及以上同色连通块会消除。
- 若一次射击未消除，则顶部下压计数减少。
- 触底（到玩家警戒线）后游戏结束并进入排行榜。

## 排行榜
- 大小 `5`，初始分数为 `0`。
- 当前成绩进入榜单后高亮。

## 可调参数（含代码行）
- 发射速度 `BUBBLE_SPEED`（46）
- 逻辑步长 `UPDATE_MS`（49）
- 消除动画帧数 `POP_ANIM_FRAMES`（50）
- 每次下压前允许空击次数 `SHOTS_PER_DESCENT`（54）
- 失败警戒线 `PLAYER_LINE_Y`（55）
- 排行榜大小 `LEADERBOARD_SIZE`（52）
- 排行榜初始值 `leaderboard[LEADERBOARD_SIZE] = {0, 0, 0, 0, 0}`（143）

<div align="center">
  <a href="../../README.md" style="display: inline-block; margin: 10px 0 18px; padding: 10px 18px; border-radius: 999px; background: linear-gradient(135deg, #1f6feb, #3fb950); color: #ffffff; text-decoration: none; font-weight: 700; box-shadow: 0 4px 12px rgba(31, 111, 235, 0.25);">返回 README 主页</a>
</div>
