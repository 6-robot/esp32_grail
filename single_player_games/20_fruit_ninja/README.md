# Fruit Ninja

![Fruit Ninja](./20_fruit_ninja.jpg)

文件: [20_fruit_ninja.ino](./20_fruit_ninja.ino)

## 玩法
- 触摸滑动切水果。
- 切中炸弹后播放爆炸动画并结束本局。
- 漏果累计 `3` 次结束。
- Game Over 后自动进入排行榜。

## 控制
- 触摸: 开始 / 切水果 / 排行榜返回
- 蓝键: 标题开始 / 排行榜返回
- 绿键: 对局中回标题

## 可调参数（含代码行）
- 逻辑帧间隔 `FRAME_MS`（25）
- 初始容错次数 `START_LIVES`（36）
- 炸弹爆炸时长 `BOMB_EXPLOSION_MS`（33）
- 最大水果数 `MAX_FRUITS`（27）
- 最大粒子数 `MAX_PARTICLES`（28）
- 重力 `GRAVITY`（35）
- 前 10 秒阶段判定 `elapsed < 10000`（539）
- 前 10 秒同屏上限 `maxOnScreen = 2`（548）
- 10 秒后同屏上限 `maxOnScreen = 3`（548）
- 排行榜大小 `LEADERBOARD_SIZE = 5`（26）
- 排行榜初始值 `leaderboard[LEADERBOARD_SIZE] = {0, 0, 0, 0, 0}`（131）

<div align="center">
  <a href="../../README.md" style="display: inline-block; margin: 10px 0 18px; padding: 10px 18px; border-radius: 999px; background: linear-gradient(135deg, #1f6feb, #3fb950); color: #ffffff; text-decoration: none; font-weight: 700; box-shadow: 0 4px 12px rgba(31, 111, 235, 0.25);">返回 README 主页</a>
</div>
