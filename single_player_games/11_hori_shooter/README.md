# Horizon Shooter

![Horizon Shooter](./11_hori_shooter.jpg)

文件: [11_hori_shooter.ino](./11_hori_shooter.ino)

## 控制
- 摇杆: 移动飞机
- 蓝键: 普通射击
- 绿键: 发射必杀

## 玩法
- 初始 `3` 条命（678），初始必杀 `3` 发（686）。
- 击败敌机并进入 Boss 战，失败后结算。

## 可调参数（含代码行）
- 主循环步长 `GAME_STEP_MS`（38）
- 对象上限 `MAX_PLAYER_BULLETS` / `MAX_ENEMIES` / `MAX_ENEMY_BULLETS`（39-41）
- 普通射击冷却 `player.fireCooldownUntil = now + 120`（764）
- 必杀冷却 `player.specialCooldownUntil = now + 400`（783）
- 必杀前进距离 `specialWeapon.targetX = player.x + 100.0f`（790）
- 必杀飞行速度 `specialWeapon.x += 2.2f`（919）
- Boss 本体血量 `boss.coreHp = 24`（949）
- Boss 副炮血量 `boss.podHp[0/1] = 10`（950-951）
- Boss 开火间隔 `boss.nextVolleyAt = now + 820`（1138）
- 进入 Boss 战时机 `elapsed >= 45000`（1287）
- 敌人刷新间隔 `now - lastEnemySpawnMs < 1200`（1291）

<div align="center">
  <a href="../../README.md" style="display: inline-block; margin: 10px 0 18px; padding: 10px 18px; border-radius: 999px; background: linear-gradient(135deg, #1f6feb, #3fb950); color: #ffffff; text-decoration: none; font-weight: 700; box-shadow: 0 4px 12px rgba(31, 111, 235, 0.25);">返回 README 主页</a>
</div>
