# Tank Battle

![Tank Battle](./4_tank_battle.jpg)

文件: [4_tank_battle.ino](./4_tank_battle.ino)

## 玩法
- 按钮开始。
- 摇杆控制坦克移动和朝向，按钮发射。
- 击毁敌人得分，保护基地；基地被毁或生命耗尽失败。
- 达成关卡目标后进入下一关。

## 控制
- 摇杆: 上下左右移动并改变炮口
- 按钮: 开始 / 发射 / 下一关 / 失败后返回
- LED `47`: 菜单和结算闪烁，游戏中常亮

## 可调参数（含代码行）
- 地图参数 `TILE_SIZE` / `MAP_WIDTH` / `MAP_HEIGHT`（12-14）
- 坦克与子弹 `TANK_SIZE` / `TANK_SPEED` / `BULLET_SIZE` / `BULLET_SPEED`（16-19）
- 上限 `MAX_BULLETS` / `MAX_ENEMIES`（21-22）
- 敌人节奏 `ENEMY_SHOOT_INTERVAL` / `ENEMY_MOVE_INTERVAL`（23-24）
- 摇杆阈值 `JOYSTICK_CENTER` / `JOYSTICK_THRESHOLD`（26-27）
- 每关目标公式 `totalEnemies = 5 + level * 2`（265）
- 玩家射击冷却 `currentTime - player.lastShot < 300`（351）
- 敌人生成间隔 `currentTime - lastEnemySpawn >= 3000`（393）
- 敌人改向概率 `random(100) < 30`（496）
- 敌人射击尝试概率 `random(100) < 5`（524）

<div align="center">
  <a href="../../README.md" style="display: inline-block; margin: 10px 0 18px; padding: 10px 18px; border-radius: 999px; background: linear-gradient(135deg, #1f6feb, #3fb950); color: #ffffff; text-decoration: none; font-weight: 700; box-shadow: 0 4px 12px rgba(31, 111, 235, 0.25);">返回 README 主页</a>
</div>
