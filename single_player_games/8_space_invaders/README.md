# Space Invaders (ESP32-S3)

![Space Invaders (ESP32-S3)](./8_space_invaders.jpg)

文件: `8_space_invaders.ino`

## 控制
- 摇杆左右: 移动飞船
- 绿键: 发射
- 蓝键: 开始 / 重开

## 玩法
- 击落敌人得分并推进关卡。
- 玩家被击中会掉命并触发震动。

## 可调参数（含代码行）
- 全局节拍 `GAME_UPDATE_INTERVAL_MS`（133）
- 玩家射速 `shootCooldownMs`（127）
- 敌人移动节奏 `alienMoveIntervalMs`（117）
- 敌人射击间隔 `enemyShotIntervalMs`（131）
- 玩家子弹上限 `MAX_PLAYER_BULLETS`（53）
- 敌弹上限 `MAX_ENEMY_BULLETS`（54）
- 敌人阵列规模 `ALIEN_COLS` / `ALIEN_ROWS`（43-44）
- 音频采样率与块大小 `SAMPLE_RATE` / `AUDIO_CHUNK_SAMPLES`（26-27）

<div align="center">
  <a href="../../README.md" style="display: inline-block; margin: 10px 0 18px; padding: 10px 18px; border-radius: 999px; background: linear-gradient(135deg, #1f6feb, #3fb950); color: #ffffff; text-decoration: none; font-weight: 700; box-shadow: 0 4px 12px rgba(31, 111, 235, 0.25);">返回 README 主页</a>
</div>
