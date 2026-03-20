# Plants vs Zombies

![Plants vs Zombies](./18_plants_vs_zombies.jpg)

文件: `18_plants_vs_zombies.ino`

## 玩法
- `4` 行草地，放置射手与坚果抵挡僵尸。
- 击败 `18` 个僵尸胜利，僵尸到最左侧失败。
- 结束后先显示结算，再进入排行榜。

## 控制
- 摇杆: 移动光标
- 绿键: 放置 `Peashooter`
- 蓝键: 放置 `Wallnut`

## 可调参数（含代码行）
- 车道与网格 `LANE_COUNT` / `CELL_W` / `CELL_H` / `GRID_COLS`（32-36）
- 对象上限 `MAX_ZOMBIES` / `MAX_PEAS`（41-42）
- 胜利目标 `targetDefeats = 18`（130）
- 开局阳光 `sunPoints = 125`（744）
- 阳光增长 `nextSunMs = millis() + 3500`（748）
- 阳光增长步长 `sunPoints += 25`（1400）
- 阳光下一次刷新 `nextSunMs = now + 3600`（1401）
- 子弹速度 `peas[i].x += 6.0f`（952）
- 子弹碰撞阈值 `abs((int)(peas[i].x - zombies[z].x)) < 14`（959）
- 僵尸血量 `zombies[i].hp = 5`（885）
- 僵尸速度 `zombie.x -= 0.78f`（1001）
- 僵尸攻击间隔 `zombie.attackAtMs = now + 700`（994）
- 生成间隔 `nextSpawnMs = now + random(1800, 3400)`（1406）
- 排行榜大小 `LEADERBOARD_SIZE = 5`（131）
- 排行榜初始值 `leaderboard[LEADERBOARD_SIZE] = {0, 0, 0, 0, 0}`（136）

<div align="center">
  <a href="../../README.md" style="display: inline-block; margin: 10px 0 18px; padding: 10px 18px; border-radius: 999px; background: linear-gradient(135deg, #1f6feb, #3fb950); color: #ffffff; text-decoration: none; font-weight: 700; box-shadow: 0 4px 12px rgba(31, 111, 235, 0.25);">返回 README 主页</a>
</div>
