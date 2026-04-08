# Angry Birds

![Angry Birds](./19_angry_birds.jpg)

文件: [19_angry_birds.ino](./19_angry_birds.ino)

## 玩法
- 标题页触摸或蓝键开始。
- 拖拽弹弓发射小鸟，清空猪即过关。
- 鸟用尽仍有猪存活则失败，Game Over 后进入排行榜。

## 控制
- 触摸: 拖拽/发射
- 蓝键: 标题开始
- 绿键: 对局中回标题

## 可调参数（含代码行）
- 帧间隔 `FRAME_MS`（31）
- 发射系数 `LAUNCH_SCALE`（45）
- 最大拉伸 `MAX_PULL`（44）
- 重力 `GRAVITY`（41）
- 阻尼 `DRAG`（42）
- 鸟碰撞半径 `BIRD_R`（40）
- 初始鸟数 `INITIAL_BIRDS`（43）
- 砖块上限 `MAX_BLOCKS`（34）
- 猪上限 `MAX_PIGS`（35）
- 粒子上限 `MAX_PARTICLES`（36）
- 砖块受伤系数 `damage = (int)(speed * 8.5f)`（513）
- Game Over 停留时长 `millis() - stateStartMs >= 1200`（1022）

<div align="center">
  <a href="../../README.md" style="display: inline-block; margin: 10px 0 18px; padding: 10px 18px; border-radius: 999px; background: linear-gradient(135deg, #1f6feb, #3fb950); color: #ffffff; text-decoration: none; font-weight: 700; box-shadow: 0 4px 12px rgba(31, 111, 235, 0.25);">返回 README 主页</a>
</div>
