# Zuma

![Zuma](./16_zuma.jpg)

文件: `16_zuma.ino`

## 控制
- 摇杆: 瞄准
- 蓝键: 开始 / 发射 / 排行榜返回
- 绿键: 交换当前球与下一球

## 玩法
- 球链沿轨道推进，发射球插入链条并触发消除。
- 3 连及以上同色触发消除与连锁。
- 链头到终点后 Game Over 并进入排行榜。

## 计分
- 单次消除: `groupSize * 12 + 8`（593）
- 清波奖励: `120 + wave * 20`（845）

## 可调参数（含代码行）
- 主更新步长 `UPDATE_MS`（38）
- 最大链长 `MAX_CHAIN_BALLS`（41）
- 发射速度 `SHOT_SPEED`（44）
- 发射冷却 `SHOT_COOLDOWN_MS`（46）
- 瞄准死区 `AIM_DEADZONE`（45）
- 插入动画 `INSERT_ANIM_FRAMES` / `INSERT_ANIM_FRAME_MS`（51-52）
- 消除动画 `POP_ANIM_FRAMES` / `POP_ANIM_FRAME_MS`（53-54）
- 回接节奏 `RECONNECT_HOLD_MS` / `RECONNECT_RETURN_MS`（55-56）
- 每波初始链长公式 `24 + wave * 3`（489）
- 排行榜大小 `LEADERBOARD_SIZE`（48）

<div align="center">
  <a href="../../README.md" style="display: inline-block; margin: 10px 0 18px; padding: 10px 18px; border-radius: 999px; background: linear-gradient(135deg, #1f6feb, #3fb950); color: #ffffff; text-decoration: none; font-weight: 700; box-shadow: 0 4px 12px rgba(31, 111, 235, 0.25);">返回 README 主页</a>
</div>
