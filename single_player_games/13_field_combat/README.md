# Field Combat

![Field Combat](./13_field_combat.jpg)

文件: [13_field_combat.ino](./13_field_combat.ino)

## 控制
- 摇杆: 移动指挥车
- 蓝键: 普通射击
- 绿键: 在俘获条件满足时执行牵引俘获

## 玩法
- 将残血敌人牵引为友军，友军会跟随并协同作战。
- 玩家生命归零后结算并进入排行榜。

## 可调参数（含代码行）
- 主更新步长 `GAME_STEP_MS`（34）
- 玩家速度上限 `PLAYER_MAX_SPEED_X` / `PLAYER_MAX_SPEED_Y`（35-36）
- 玩家加速度 `PLAYER_ACCEL_X` / `PLAYER_ACCEL_Y`（37-38）
- 移动阻尼 `PLAYER_DAMPING`（39）
- 俘获圈偏移与半径 `CAPTURE_ZONE_OFFSET_Y` / `CAPTURE_ZONE_HIT_RADIUS`（40-41）
- 接触转化判定半径 `CAPTURE_CONTACT_RADIUS`（42）
- 对象上限 `MAX_PLAYER_BULLETS` / `MAX_ENEMY_BULLETS` / `MAX_ENEMIES` / `MAX_ALLIES`（44-47）
- 通关目标 `TARGET_PROGRESS`（49）
- 排行榜大小 `LEADERBOARD_SIZE`（43）

<div align="center">
  <a href="../../README.md" style="display: inline-block; margin: 10px 0 18px; padding: 10px 18px; border-radius: 999px; background: linear-gradient(135deg, #1f6feb, #3fb950); color: #ffffff; text-decoration: none; font-weight: 700; box-shadow: 0 4px 12px rgba(31, 111, 235, 0.25);">返回 README 主页</a>
</div>
