# Billiards

![Billiards](./15_billiards.jpg)

文件: [15_billiards.ino](./15_billiards.ino)

## 控制
- 摇杆: 调整击球方向
- 蓝键: 按住蓄力，松开发射
- 绿键: 对局中返回标题
- 标题页蓝键: 开始
- 标题页绿键: 清空排行榜
- 排行榜页蓝键: 再来一局，绿键: 回标题

## 玩法
- 清空目标球（除白球）完成本局。
- 白球入袋会扣分并在停球后重生。
- 有最大出杆次数限制，超出则结束。
- 结束后先显示结算页，再进入排行榜。

## 排行榜
- 大小 `5`，初始分数全 `0`（80）。
- 当前成绩进入榜单后高亮并带标记（844-853）。

## 可调参数（含代码行）
- 球与袋口半径 `BALL_R` / `POCKET_R`（35-36）
- 球数量上限 `MAX_BALLS`（37）
- 最大出杆数 `MAX_SHOTS`（41）
- 物理参数 `FRICTION` / `MIN_SPEED` / `MAX_SPEED`（44-46）
- 蓄力参数 `CHARGE_RATE` / `MAX_CHARGE`（47-48）
- 固定物理步长 `STEP_MS`（39）
- 结算页停留时间 `GAME_OVER_SPLASH_MS`（40）
- 白球入袋扣分 `score -= 15`（453）
- 目标球得分 `bonus = 25`，黑球 `45`（463）
- 连续入袋连击加分 `(pocketedThisShot - 1) * 10`（464）
- 清台奖励 `(MAX_SHOTS - shotsUsed) * 6`（552）
- 发射最小蓄力阈值 `shotCharge < 0.08f`（507）

<div align="center">
  <a href="../../README.md" style="display: inline-block; margin: 10px 0 18px; padding: 10px 18px; border-radius: 999px; background: linear-gradient(135deg, #1f6feb, #3fb950); color: #ffffff; text-decoration: none; font-weight: 700; box-shadow: 0 4px 12px rgba(31, 111, 235, 0.25);">返回 README 主页</a>
</div>
