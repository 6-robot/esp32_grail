# Bomberman

![Bomberman](./9_bomberman.jpg)

文件: [9_bomberman.ino](./9_bomberman.ino)

## 玩法
- 放置炸弹、清理砖块和敌人，完成关卡后进入下一关。
- 角色有生命与短暂无敌时间。

## 可调参数（含代码行）
- 地图尺寸 `ARENA_COLS` / `ARENA_ROWS` / `TILE_SIZE`（34-36）
- 主更新步长 `GAME_UPDATE_INTERVAL_MS`（44）
- 玩家/敌人步进节奏 `PLAYER_STEP_MS` / `ENEMY_STEP_MS`（45-46）
- 炸弹与火焰 `BOMB_FUSE_MS` / `FLAME_MS`（47-48）
- 关卡节奏 `STAGE_CLEAR_MS` / `PLAYER_INVULN_MS`（49-50）
- 对象上限 `MAX_BOMBS` / `MAX_FLAMES` / `MAX_ENEMIES` / `MAX_POWERUPS`（51-54）
- 音频参数 `SAMPLE_RATE` / `AUDIO_CHUNK_SAMPLES`（26-27）

<div align="center">
  <a href="../../README.md" style="display: inline-block; margin: 10px 0 18px; padding: 10px 18px; border-radius: 999px; background: linear-gradient(135deg, #1f6feb, #3fb950); color: #ffffff; text-decoration: none; font-weight: 700; box-shadow: 0 4px 12px rgba(31, 111, 235, 0.25);">返回 README 主页</a>
</div>
