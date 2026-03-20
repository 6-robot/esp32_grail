# Gold Miner

![Gold Miner](./6_gold_miner.jpg)

文件: `6_gold_miner.ino`

## 控制
- 蓝键: 标题开始 / 发射钩子 / 失败后重开
- 绿键: 炸掉当前回收物体 / 通关后下一关 / 失败后回标题

## 玩法
- 钩子摆动后发射，抓取不同重量与分值的物体。
- 每关有目标分与倒计时。
- 失败后进入排行榜（Top 5，初始全 0，当前成绩高亮）。

## 可调参数（含代码行）
- 钩子旋转中心 `HOOK_PIVOT_Y`（46）
- 发射/回收/摆动 `HOOK_SHOOT_SPEED` / `HOOK_RETRACT_SPEED` / `HOOK_SWING_SPEED`（49-51）
- 摆角限制 `HOOK_SWING_LIMIT`（52）
- 目标分增长 `targetScore = 650 + (levelIndex - 1) * 280`（496）
- 关卡时间 `timeLeft = max(35, 60 - (levelIndex - 1) * 2)`（497）
- 炸弹数量 `bombCount = min(6, 3 + (levelIndex - 1) / 2)`（498）
- 飘字时长 `durationMs = 900`（632, 641）
- 爆炸时长 `durationMs = 220`（571, 580）
- 排行榜大小 `LEADERBOARD_SIZE = 5`（40）

<div align="center">
  <a href="../../README.md" style="display: inline-block; margin: 10px 0 18px; padding: 10px 18px; border-radius: 999px; background: linear-gradient(135deg, #1f6feb, #3fb950); color: #ffffff; text-decoration: none; font-weight: 700; box-shadow: 0 4px 12px rgba(31, 111, 235, 0.25);">返回 README 主页</a>
</div>
