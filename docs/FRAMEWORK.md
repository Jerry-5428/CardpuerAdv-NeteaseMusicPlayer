# Cardputer ADV 网易云音乐播放器 - 技术框架

> 状态：待确认。确认后再开始创建工程、写代码、搭建服务。
>
> 目标设备：M5Stack Cardputer-Adv（ESP32-S3FN8）
>
> 参考音频解析项目：[Suxiaoqinx/Netease_url](https://github.com/Suxiaoqinx/Netease_url)

---

## 1. 结论先行

这个项目**不能只靠 Cardputer 固件独立完成**，必须有一台局域网内可访问的伴生服务：

1. **账号/元数据服务**：登录、歌单、搜索、红心、私人 FM、歌词、歌曲 URL。
   - 原版 `Binaryify/NeteaseCloudMusicApi` 仓库 README 已注明“保护版权，此仓库不再维护”，不能作为唯一依赖。
   - 计划使用仍在维护的 fork：[NeteaseCloudMusicApiEnhanced/api-enhanced](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced)。
2. **高音质解析服务（可选但推荐）**：`Suxiaoqinx/Netease_url`，负责用服务端 VIP Cookie 解析无损/高音质直链。
   - 它只有 `/song`、`/search`、`/playlist`、`/album`、`/download`，没有登录、红心、私人 FM 等账号接口，所以不能单独承担全部功能。
   - 固件支持两种音频来源：设备 Cookie 直连 Enhanced API，或走 Netease_url 解析；默认 `auto`，前者失败再回退后者。

固件侧的核心约束：

- **ESP32-S3FN8 没有 PSRAM**，只有 512 KB SRAM。
- `ESP32-audioI2S` 默认音频输入缓冲约 **704 KB**（65535×10 + 65535），无 PSRAM 时不可能分配成功。
- 因此必须使用**裁剪缓冲后的音频库**（计划 32 KB + 4 KB reserve），或改用更轻量的解码库。这是 M1 阶段第一项验证。
- 没有 PSRAM 也意味着：歌单必须分页/落盘，JSON 必须使用过滤器，UI 不能长期占用全屏 16 位 Canvas。

---

## 2. 硬件取证

以下结论全部来自官方文档或官方库源码，不靠猜 pin。

### 2.1 主控与存储

| 项目 | 取证结论 | 来源 |
|---|---|---|
| 主控 | Stamp-S3A，ESP32-S3FN8，双核 LX7 240 MHz | [Cardputer-Adv 官方文档](https://docs.m5stack.com/en/core/Cardputer-Adv) |
| Flash | 8 MB（FN8 的 `8`） | 同上；[Espressif 封装丝印规则](https://docs.espressif.com/projects/esp-packaging/en/latest/esp32s3/01-marking/index_chip.html) |
| PSRAM | **无**。`FN8` 属于 “In-package Flash, No PSRAM”；`FN` 表示 85°C，`8` 表示 8 MB Flash | [Espressif 封装丝印规则](https://docs.espressif.com/projects/esp-packaging/en/latest/esp32s3/01-marking/index_chip.html) |
| SRAM | 512 KB | [ESP32-S3 Datasheet](https://documentation.espressif.com/esp32-s3_datasheet_en.pdf) |
| microSD | SPI：CLK=G40，MOSI=G14，MISO=G39，CS=G12；FAT32 | [官方 PinMap](https://docs.m5stack.com/en/core/Cardputer-Adv)；[官方 SD 例程](https://docs.m5stack.com/en/arduino/m5cardputer/sdcard) |
| 电池 | ADC=G10；可读百分比/电压；**无法读取充电状态和电流** | [官方 Battery 文档](https://docs.m5stack.com/en/arduino/m5cardputer/battery) |

### 2.2 显示

| 项目 | 取证结论 | 来源 |
|---|---|---|
| 屏幕 | ST7789V2，1.14 英寸，240×135 | [官方文档](https://docs.m5stack.com/en/core/Cardputer-Adv) |
| 引脚 | BL=G38，RST=G33，RS=G34，DAT=G35，SCK=G36，CS=G37 | [官方 PinMap](https://docs.m5stack.com/en/core/Cardputer-Adv) |
| 驱动 | M5GFX / M5Unified 自动识别，不手写 ST7789 初始化 | [官方 Display 文档](https://docs.m5stack.com/en/arduino/m5cardputer/display) |
| 中文字体 | M5GFX 内置 `fonts::efontCN_10/12/14/16`，可用 `efontCN_12` | [M5GFX 字体声明](https://github.com/m5stack/M5GFX/blob/master/src/lgfx/v1/lgfx_fonts.hpp) |
| 注意 | G38 同时出现在 LCD_BL 与 RGB LED PWR_EN 行；固件不直接操作 RGB，避免与背光冲突 | [官方 PinMap](https://docs.m5stack.com/en/core/Cardputer-Adv) |

### 2.3 键盘

| 项目 | 取证结论 | 来源 |
|---|---|---|
| 键盘控制器 | TCA8418RTWR，I2C 地址 `0x34`，7×8 矩阵，56 键 | [官方 PinMap](https://docs.m5stack.com/en/core/Cardputer-Adv)；[Adafruit_TCA8418 头文件](https://github.com/m5stack/M5Cardputer/blob/master/src/utility/Adafruit_TCA8418/Adafruit_TCA8418.h) |
| 引脚 | SDA=G8，SCL=G9，INT=G11 | [官方 PinMap](https://docs.m5stack.com/en/core/Cardputer-Adv)；[TCA8418.cpp](https://github.com/m5stack/M5Cardputer/blob/master/src/utility/Keyboard/KeyboardReader/TCA8418.cpp) |
| 库支持 | M5Cardputer 库根据 `board_M5CardputerADV` 自动选择 `TCA8418KeyboardReader` | [Keyboard.cpp](https://github.com/m5stack/M5Cardputer/blob/master/src/utility/Keyboard/Keyboard.cpp) |
| API | `M5Cardputer.update()` + `M5Cardputer.Keyboard.keysState()`；含方向键/Fn/Shift/Ctrl/Enter/Backspace | [官方 Keyboard 文档](https://docs.m5stack.com/en/arduino/m5cardputer/keyboard) |
| 注意 | 主循环不能长时间阻塞，否则键盘事件会丢；下载/解析必须分片并持续调用 `M5Cardputer.update()` | 官方 Keyboard 文档明确提示 |

### 2.4 音频

| 项目 | 取证结论 | 来源 |
|---|---|---|
| Codec | ES8311，I2C 地址 `0x18` | [官方 PinMap](https://docs.m5stack.com/en/core/Cardputer-Adv)；[M5Unified 源码](https://github.com/m5stack/M5Unified/blob/master/src/M5Unified.inl) |
| I2C | SDA=G8，SCL=G9（与键盘、IMU 共用） | 官方 PinMap |
| I2S | BCLK/SCLK=G41，LRCK/WS=G43，DAC 数据/DSDIN=G42；麦克风 ASDOUT=G46 | 官方 PinMap；[M5Unified 源码](https://github.com/m5stack/M5Unified/blob/master/src/M5Unified.inl) |
| MCLK | **没有独立 MCLK 引脚**；M5Unified 将 ES8311 配置为 `MCLK=BCLK` | [M5Unified 源码](https://github.com/m5stack/M5Unified/blob/master/src/M5Unified.inl) |
| 功放/扬声器 | NS4150B + 8Ω 1W 扬声器 | 官方文档 |
| 耳机孔 | 插入 3.5 mm 耳机后，扬声器功放自动禁用 | [官方 Speaker 文档](https://docs.m5stack.com/en/arduino/m5cardputer/speaker) |
| 官方库能力 | `M5.Speaker` 只能播放 PCM/WAV，不能直接解码 MP3/FLAC | [M5Unified Speaker 文档](https://docs.m5stack.com/en/arduino/m5unified/speaker_class) |
| 第三方解码 | `ESP32-audioI2S` 支持 ESP32-S3、MP3/AAC/FLAC/WAV，可播放 SD 文件或 HTTP 流 | [ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S) |
| 无 PSRAM 风险 | 该库默认 `AudioBuffer` 为 65535×10 + 65535 ≈ 704 KB，**512 KB SRAM 放不下** | [buffers.hpp](https://github.com/schreibfaul1/ESP32-audioI2S/blob/master/src/buffers.hpp) |
| 应对 | 固定使用裁剪版音频库（32 KB + 4 KB），或 M1 验证失败时切换 `ESP8266Audio` 的本地文件播放链路 | 本框架决策，M1 验证 |

### 2.5 其他

| 项目 | 结论 | 来源 |
|---|---|---|
| IMU | BMI270，I2C G8/G9；v1 不使用 | 官方文档 |
| 红外 | IR TX=G44；v1 不使用 | 官方文档 |
| 下载模式 | 关机状态下按住 G0，插 USB-C 后松开 | [官方编译烧录文档](https://docs.m5stack.com/en/arduino/m5cardputer/program) |
| PlatformIO | 官方示例：`platform=espressif32@6.7.0`，`board=esp32-s3-devkitc-1`，`framework=arduino` | 同上 |
| 3.5 mm | 插入耳机后不要期待内置扬声器发声 | 官方 Speaker 文档 |

---

## 3. 总体架构

```text
┌──────────────────────────── Cardputer-Adv 固件 ────────────────────────────┐
│                                                                            │
│  Input (TCA8418)      UI (M5GFX, 240×135, efontCN_12)                      │
│       │                    │                                               │
│       └────────► App / Screen Manager ◄──────────────┐                     │
│                         │                           │                     │
│                  Player / Queue                Settings / Login           │
│                  │        │                         │                     │
│          AudioEngine   Lyrics                  SD Store                  │
│          (ES8311 +     (LRC)                   (cookie/cache/            │
│           audioI2S)                             lyrics/downloads)        │
│                  │                                  │                     │
│                  └────────── NetClient ─────────────┘                     │
│                          (HTTP/HTTPS + JSON filter)                       │
└──────────────────────────────┬─────────────────────────────────────────────┘
                               │ LAN
              ┌────────────────┴─────────────────┐
              │                                  │
   ┌──────────▼──────────┐            ┌──────────▼──────────┐
   │ NCM API Enhanced    │            │ Netease_url          │
   │ :3000               │            │ :5000                │
   │ 登录/搜索/歌单/红心  │            │ /song 高音质解析     │
   │ 私人FM/歌词/URL      │            │ /download 直链下载   │
   └─────────────────────┘            └─────────────────────┘
```

设计原则：

- **固件不实现网易云 WeApi 加密**。加密、Cookie、风控、音质解析全部放在伴生服务，固件只做 HTTP 客户端。
- **SD 卡是主存储**。Cookie、缓存、歌词、下载歌单、离线索引都在 SD。
- **播放走本地文件**。先解析 URL，再下载到 SD `.part`，完成后原子重命名，再交给音频库播放。这样进度条、seek、缓存、离线都可靠。
- **所有网络请求分页 + 过滤器**。不把 1000 首歌的 JSON 一次性放进 RAM。

---

## 4. 固件模块划分

计划目录（确认后创建）：

```text
firmware/
  platformio.ini
  include/
    config.h                 # WiFi、API 地址、默认音质、引脚常量
  src/
    main.cpp
    app/App.h/.cpp           # 状态机、主循环、屏幕调度
    hw/Board.h/.cpp          # M5Cardputer、SD、ES8311、电池、I2C
    input/Input.h/.cpp       # 键盘映射、长按重复、文本输入
    ui/Ui.h/.cpp             # 绘制原语、状态栏、列表、弹窗
    ui/screens/*.h/.cpp      # Home/List/Player/Search/Login/Settings
    net/Http.h/.cpp          # HTTP/HTTPS、超时、重试、JSON 过滤器
    net/NeteaseApi.h/.cpp    # Enhanced API 客户端
    net/AudioResolver.h/.cpp # Netease_url + /song/url/v1 回退
    store/SdStore.h/.cpp     # 目录、Cookie、缓存、下载、索引
    player/Player.h/.cpp     # 队列、播放模式、FM、红心、打卡
    audio/AudioEngine.h/.cpp # ES8311 + audioI2S 播放/进度/音量/seek
    audio/Lyrics.h/.cpp      # LRC 解析与同步
  lib/
    ESP32-audioI2S/          # 裁剪缓冲后的 vendored 库（GPL-3.0）
server/
  docker-compose.yml          # Enhanced API + Netease_url
  cookie.example.txt          # 解析服务用的 VIP Cookie 模板
```

模块职责：

| 模块 | 职责 | 关键点 |
|---|---|---|
| `Board` | 初始化 M5Cardputer、SD、I2C、ES8311、电池 | `cfg.internal_spk=false`，避免 M5.Speaker 占用 I2S；自己写 ES8311 寄存器 |
| `Input` | 把按键转成事件 | 方向键来自 Fn 层；长按连续触发；文本输入模式 |
| `Ui` | 240×135 布局、中文字体、局部刷新 | 不使用 16 位全屏 Canvas；列表只重绘变化行 |
| `Http` | GET/POST、URL 编码、超时、重试 | 同时只保持一个 TLS 连接；响应使用 `JsonDocument` filter |
| `NeteaseApi` | 登录、搜索、歌单、红心、FM、歌词 | 所有请求带 `cookie` 参数；分页 |
| `AudioResolver` | 获取可播放直链 | `device` / `resolver` / `auto` 三种模式；音质回退 |
| `SdStore` | Cookie/配置/缓存/下载/歌词 | 原子写；缓存 LRU；空间检查 |
| `Player` | 队列与播放控制 | 顺序/循环/单曲/随机；FM 续取；红心同步 |
| `AudioEngine` | 解码播放 | 本地文件优先；`audio.loop()` 不阻塞 |
| `Lyrics` | LRC 解析与当前行 | 只保留必要行，按时间二分/线性推进 |

---

## 5. SD 卡布局

```text
/netease/
  wifi.json                 # 可选；不存在时用 config.h
  settings.json             # 音质、音量、播放模式、缓存上限、API 地址
  cookie.json               # 登录 Cookie + uid + nickname + vipType
  cache/
    audio/<songId>_<level>.<mp3|flac>
    tmp/<songId>.part
  lyrics/<songId>.lrc
  likes.json                # 红心歌曲 ID 缓存
  downloads/
    index.json              # 下载索引
    <playlistId>_<safeName>/
      001_<artist>-<title>.<ext>
```

### 5.1 `cookie.json`

```json
{
  "cookie": "MUSIC_U=...; __csrf=...;",
  "uid": 123456789,
  "nickname": "用户昵称",
  "vipType": 11,
  "savedAt": 1760000000
}
```

- 首次 QR 登录成功后写入；重启后直接读取并调用 `/login/status` 校验。
- 校验失败：提示重新登录，不自动删除旧文件（便于排查）。
- 退出登录：删除 `cookie.json`，同时清空内存中的 uid。
- 日志中禁止打印 `MUSIC_U`、`__csrf`。

### 5.2 `settings.json`

```json
{
  "apiBase": "http://192.168.1.10:3000",
  "resolverBase": "http://192.168.1.10:5000",
  "audioSource": "auto",
  "quality": "exhigh",
  "volume": 14,
  "playMode": "sequence",
  "cacheLimitMB": 512,
  "lyricsTranslation": false
}
```

- `audioSource`：`auto` / `device` / `resolver`。
- `quality`：`standard` / `exhigh` / `lossless` / `hires`。默认 `exhigh`（无 PSRAM 更稳）。
- 文件不存在时由固件生成默认值。

### 5.3 缓存策略

- 当前歌曲缓存键：`<songId>_<level>`；同一首歌不同音质分开缓存。
- 下载先写 `.part`；校验 `Content-Length` 和文件大小后 `rename`。
- 缓存超限：按最后访问时间淘汰；正在播放的文件加锁不淘汰。
- 下载歌单目录不受缓存上限影响，由用户手动删除。
- SD 剩余空间低于 100 MB 时停止缓存并提示。

---

## 6. 网易云 API 设计

### 6.1 登录

使用 Enhanced API 的二维码登录：

| 步骤 | 请求 | 说明 |
|---|---|---|
| 1 | `GET /login/qr/key?timestamp=...` | 返回 `data.unikey` |
| 2 | `GET /login/qr/create?key=<key>&qrimg=false&timestamp=...` | 返回 `data.qrurl` |
| 3 | 固件把 `qrurl` 生成 QR 码并显示 | 135×135 区域 |
| 4 | 每 2 秒 `GET /login/qr/check?key=<key>&timestamp=...` | `800` 过期、`801` 等待、`802` 已扫码、`803` 成功 |
| 5 | `803` 时保存响应里的 `cookie` | 写入 `/netease/cookie.json` |
| 6 | `GET /login/status?cookie=...` | 取 `profile.userId`、`nickname`、`vipType` |

兼容方案：

- 如果 SD 根目录存在 `/netease/cookie.txt`（用户从浏览器复制），启动时直接导入并校验。
- 二维码登录失败时，提示用户用读卡器放入 `cookie.txt`。
- 不在固件里做手机号/密码登录，避免验证码和加密复杂度。

### 6.2 元数据与账号接口

| 功能 | 请求 | 关键返回 |
|---|---|---|
| 搜索歌曲 | `GET /cloudsearch?keywords=<q>&type=1&limit=30&offset=0&cookie=...` | `result.songs[]` |
| 热搜（中文入口） | `GET /search/hot/detail` | 热搜词列表 |
| 用户歌单 | `GET /user/playlist?uid=<uid>&limit=100&offset=0&cookie=...` | `playlist[]` |
| 歌单详情 | `GET /playlist/detail?id=<id>&cookie=...` | `playlist.name/trackCount` |
| 歌单歌曲 | `GET /playlist/track/all?id=<id>&limit=100&offset=<n>&cookie=...` | `songs[]` |
| 歌曲详情 | `GET /song/detail?ids=<id1,id2,...>&cookie=...` | `songs[]` |
| 歌词 | `GET /lyric?id=<id>&cookie=...` | `lrc.lyric`、`tlyric.lyric` |
| 红心列表 | `GET /likelist?uid=<uid>&cookie=...` | `ids[]` |
| 红心/取消 | `GET /like?id=<id>&like=true|false&cookie=...` | `code` |
| 私人 FM | `GET /personal_fm?timestamp=...&cookie=...` | `data[]` |
| 每日推荐 | `GET /recommend/songs?cookie=...` | `data.dailySongs[]` |
| 听歌打卡 | `GET /scrobble?id=<id>&time=<sec>&cookie=...` | 播放 30 秒后上报 |

分页规则：

- 歌单歌曲每页 100 首；首页先取 100 首立即显示，后台继续取下一页。
- 搜索结果只保留前 30 首，避免 RAM 压力。
- 红心列表可能上千首：先取 ID 列表，再分批 `/song/detail`（每批 100），存入 `likes.json` 和内存环形队列。

### 6.3 音频 URL 解析

三种模式：

| 模式 | 路径 | 适用 |
|---|---|---|
| `device` | `GET /song/url/v1?id=<id>&level=<level>&cookie=<设备Cookie>` | 用户自己的 VIP 生效；推荐默认 |
| `resolver` | `GET/POST <resolverBase>/song`，参数 `id`、`level`、`type=url` | 用服务端 `cookie.txt` 的 VIP 解析 |
| `auto` | 先 `device`，失败再 `resolver` | 兼容两种部署 |

音质回退：

```text
请求 quality
  → 如果失败/url 为空：
      lossless → hires → exhigh → standard
  → 仍失败：显示“VIP/版权限制，无法播放”
```

- 记录实际返回的 `level`、`type`、`size`、`br`，播放页显示音质徽标。
- 非 VIP 返回低码率或空 URL 时不无限重试；允许用户切换 `standard` 再试。
- Netease_url 的 `/song` 返回结构：`{status, success, data:{url, level, type, size, ...}}`。
- Enhanced API 的 `/song/url/v1` 返回结构：`{code, data:[{url, level, type, size, br, fee, ...}]}`。

### 6.4 错误处理

| 情况 | 行为 |
|---|---|
| WiFi 未连接 | 进入离线模式，只显示缓存/下载 |
| API 超时 | 重试 2 次；仍失败显示可返回的错误页 |
| Cookie 失效 | 清除内存登录态，提示重新扫码；保留 SD 文件 |
| `url` 为空 | 标记“VIP/版权限制”，跳到下一首或停在当前 |
| SD 空间不足 | 停止缓存，提示清理 |
| 音频解码失败 | 删除损坏缓存，重新解析一次；仍失败则跳过 |
| 服务端 500 | 显示服务端地址和错误摘要，不打印 Cookie |

---

## 7. 播放核心设计

### 7.1 播放链路

```text
选歌
  → 查 /netease/cache/audio/<id>_<level>.*
      ├─ 命中：AudioEngine.connecttoFS(SD, path)
      └─ 未命中：
            → AudioResolver.resolve(id, level)
            → Http.download(url, /cache/tmp/<id>.part, 进度回调)
            → 校验大小/格式
            → rename 为正式缓存
            → AudioEngine.connecttoFS(SD, path)
  → audio.loop() 持续调用
  → 每 500 ms 刷新进度/歌词
  → 播放 30 s 后 /scrobble
```

为什么先完整下载再播放：

- 无 PSRAM，HTTP 流式解码 + TLS + SD 写入同时进行内存风险高。
- 下载到 SD 后，seek、进度、缓存、离线、下载歌单全部统一。
- 代价是首次播放有等待；UI 显示下载进度和预计大小，可取消。

### 7.2 播放模式

- 顺序播放：播完当前，下一首；到末尾停止。
- 列表循环：到末尾回到第一首。
- 单曲循环：当前曲循环。
- 随机播放：Fisher-Yates 生成索引队列；避免连续重复。
- 私人 FM：队列不足 2 首时调用 `/personal_fm` 补充；FM 模式不显示“上一首”语义，改为“换一首”。

### 7.3 红心模式

- 入口：首页“红心歌曲”。
- 首次进入：`/likelist` 取 ID；分批 `/song/detail` 补全标题/歌手/时长；缓存到 `likes.json`。
- 播放页按 `f`：调用 `/like`，立即更新 UI；失败回滚并提示。
- 红心列表播放时，队列就是红心列表；取消红心后从列表移除但不打断当前播放。

### 7.4 歌词

- 播放前请求 `/lyric`，保存 `/netease/lyrics/<id>.lrc`。
- 解析 `[mm:ss.xx]`，支持一行多时间标签；忽略空行和元信息标签。
- 播放页显示当前行 + 下一行；可选显示翻译（`tlyric`）。
- 无歌词时显示“暂无歌词”，不阻塞播放。
- 中文歌词使用 `efontCN_12`；长行按像素宽度截断或滚动。

### 7.5 下载歌单

- 入口：歌单详情页按 `d`，确认后开始。
- 逐首：解析 URL → 下载到 `downloads/<playlistId>_<safeName>/`。
- 已存在且大小一致：跳过。
- 失败曲目记录原因（VIP/版权/网络），继续下一首。
- 结束显示：成功 N 首、跳过 M 首、失败 K 首；失败清单可滚动查看。
- 下载过程必须分片并持续 `M5Cardputer.update()` + `audio.loop()`，否则键盘/播放会卡死。

---

## 8. UI 与键盘

### 8.1 屏幕布局

240×135，横屏，`efontCN_12`，状态栏 14 px，列表行高 16 px。

首页：

```text
┌──────────────────────────────────────┐
│ WiFi 82%  14:32        ♥  ⏵  ⇄      │
│ > 搜索歌曲                           │
│   私人 FM                            │
│   我的歌单                           │
│   红心歌曲                           │
│   每日推荐                           │
│   本地缓存 / 下载                    │
│   设置                               │
└──────────────────────────────────────┘
```

播放页：

```text
┌──────────────────────────────────────┐
│ WiFi 82%  14:32      ♥  ⏵  ⇄  HQ    │
│ 晴天                                 │
│ 周杰伦 · 叶惠美                      │
│ ██████████░░░░░░  01:23 / 04:29      │
│ 故事的小黄花                         │
│ 从出生那年就飘着                     │
│ 空格播放  N下一  F红心  L歌词  D下载 │
└──────────────────────────────────────┘
```

登录页：

```text
┌──────────────────────────────────────┐
│ 扫码登录网易云音乐                   │
│ ┌──────────┐  1. 手机网易云扫码      │
│ │          │  2. 等待确认            │
│ │  QR CODE │  3. 成功后 Cookie 写入  │
│ │          │     SD 卡               │
│ └──────────┘  Fn+` 返回              │
└──────────────────────────────────────┘
```

### 8.2 键盘映射

卡片机没有独立方向键，方向键在 Fn 层：

| 操作 | 按键 |
|---|---|
| 上/下/左/右 | `Fn` + `;` / `.` / `,` / `/` |
| 确认/进入 | `Enter` |
| 返回 | `Fn` + `` ` ``（Esc） |
| 删除 | `Backspace` / `Fn` + `Backspace` |
| 播放/暂停 | `Space` |
| 下一首 | `N` |
| 上一首 | `B` |
| 红心 | `F` |
| 歌词开关 | `L` |
| 下载当前/歌单 | `D` |
| 播放模式 | `O` |
| 音量 -/+ | `-` / `=` |
| 进度 -/+ 5 秒 | `Fn` + `,` / `Fn` + `/` |
| 搜索 | `S` |
| 私人 FM | `R` |
| 红心模式 | `T` |
| 我的歌单 | `M` |
| 设置 | `,` 长按或首页菜单 |

文本输入模式：

- 进入搜索后，字母/数字直接输入，`Enter` 搜索，`Esc` 取消。
- 键盘只有 ASCII，**不能直接输入中文**。
- 中文搜索入口：
  1. 首页搜索页显示 `/search/hot/detail` 热搜词，方向键选择后直接搜索。
  2. 支持英文、拼音、歌手英文名。
  3. 后续可选：手机网页输入（Cardputer 开 AP/网页）作为增强，不纳入 v1。
- 中文歌名显示不受影响，使用 `efontCN_12`。

### 8.3 交互细节

- 下载/解析时显示进度条，可按 `Esc` 取消。
- 长按方向键 500 ms 后每 120 ms 重复。
- 弹窗优先级：确认框 > 错误提示 > Toast。
- 所有耗时操作分片，每片不超过 50 ms，然后调用 `M5Cardputer.update()`。

---

## 9. 内存与性能预算

无 PSRAM，按 512 KB SRAM 规划：

| 项 | 预算 | 说明 |
|---|---|---|
| M5GFX 8-bit Canvas | 32.4 KB | 240×135×1 字节；或改为局部直绘 |
| 音频输入缓冲 | 32 KB + 4 KB | 裁剪版 audioI2S |
| 音频 DMA/解码 | 30–60 KB | MP3 较低；FLAC 更高 |
| TLS 单连接 | 35–45 KB | 只在 API 请求期间存在 |
| JSON 过滤器文档 | 8–16 KB | 分页 + filter |
| 歌词 | 10–15 KB | 限制最大行数 |
| 队列/UI 状态 | 10–20 KB | 只存必要字段 |
| 余量 | 100 KB+ | 目标播放时 free heap > 40 KB |

策略：

- 歌单不整包解析；每页 100 首，解析后立即写入 SD 索引或队列。
- 字符串使用固定长度 `char[]`，避免大量 `String` 碎片。
- HTTP 响应使用 `DeserializationOption::Filter`，只保留需要的字段。
- 播放时关闭 WiFi？不关闭，需要 FM/歌词；但避免同时进行 TLS 下载和播放。下载在播放前完成。
- 默认 `exhigh` MP3；`lossless`/`hires` 作为设置项，M1/M3 实测后再决定是否默认开放。
- 如果 FLAC 解码内存不足：捕获失败 → 自动降级 `exhigh` → 提示用户。

---

## 10. 构建与部署

### 10.1 PlatformIO

```ini
[env:m5stack-cardputer-adv]
platform = espressif32@6.7.0
board = esp32-s3-devkitc-1
framework = arduino
upload_speed = 1500000
monitor_speed = 115200
board_build.flash_size = 8MB
board_build.partitions = huge_app.csv
build_flags =
  -DESP32S3
  -DBOARD_HAS_PSRAM=0
  -DARDUINO_USB_CDC_ON_BOOT=1
  -DARDUINO_USB_MODE=1
lib_deps =
  m5stack/M5Cardputer@^1.1.1
  bblanchon/ArduinoJson@^7.4.3
  ricmoo/QRCode@^0.0.1
# 裁剪版 ESP32-audioI2S 放在 lib/ESP32-audioI2S，PlatformIO 自动扫描 lib/
```

说明：

- 官方文档推荐 `espressif32@6.7.0` + `esp32-s3-devkitc-1`，Arduino 板选项选 `M5Cardputer`。
- `huge_app.csv` 给应用约 3 MB，避免 efontCN_12 + TLS + 解码库超出默认 1.25 MB。
- `ESP32-audioI2S` 是 GPL-3.0；vendored 修改版意味着本项目整体按 GPL-3.0 分发。
- 不使用 OTA，v1 用 USB 烧录。

### 10.2 伴生服务

`server/docker-compose.yml` 计划包含：

```yaml
services:
  ncm-api:
    image: moefurina/ncm-api:latest
    ports:
      - "3000:3000"
    restart: unless-stopped

  netease-url:
    build: ./Netease_url
    ports:
      - "5000:5000"
    volumes:
      - ./cookie.txt:/app/cookie.txt:ro
    restart: unless-stopped
```

- `NCM API Enhanced` 用 Docker 镜像或按官方 README 构建。
- `Netease_url` 从 [上游仓库](https://github.com/Suxiaoqinx/Netease_url) 构建，`cookie.txt` 填黑胶 VIP Cookie。
- 固件配置填电脑/服务器的局域网 IP，例如 `http://192.168.1.10:3000`。
- 如果只用设备 Cookie，不部署 `netease-url`，把 `audioSource` 设为 `device` 即可。
- 服务端和 Cardputer 在同一局域网时优先 HTTP，减少 TLS 内存和握手时间；跨公网再用 HTTPS。

### 10.3 SD 卡准备

1. FAT32 格式化。
2. 可选：在根目录建 `/netease/`，放入 `cookie.txt` 或让固件自动创建。
3. 可选：`/netease/wifi.json`、`/netease/settings.json` 可在电脑上预置。
4. 插入设备，开机。

---

## 11. 里程碑与验收

| 阶段 | 内容 | 验收 |
|---|---|---|
| M1 硬件 bring-up | 屏幕、中文、键盘、SD、电池、ES8311 测试音、裁剪音频库播放本地 MP3 | 能显示中文、方向键可用、SD 读写正常、扬声器出声、free heap 记录 |
| M2 网络与登录 | WiFi、QR 登录、Cookie 落盘、重启免登录、cookie.txt 导入 | 扫码成功；重启后 `/login/status` 通过；退出后清空 |
| M3 播放主链路 | 搜索、歌单、解析 URL、缓存下载、播放、进度/音量/seek | 能播放歌单；进度准确；缓存命中；断网可播缓存 |
| M4 账户功能 | 红心、红心列表、私人 FM、每日推荐、播放打卡 | 红心状态与手机端一致；FM 可连续续播 |
| M5 歌词与下载 | LRC 同步、翻译、下载整张歌单、失败清单 | 歌词滚动准确；下载可恢复；VIP 失败有提示 |
| M6 稳定性 | 1000 首歌单、长时间播放、SD 满、弱网、耳机插拔 | 不崩溃；free heap > 40 KB；错误可恢复 |

---

## 12. 风险与限制

1. **版权与账号风险**：本项目只做客户端，不绕过版权；高音质/下载依赖账号 VIP 与服务端 Cookie。没有 VIP 时部分歌曲无法解析是预期行为。
2. **API 稳定性**：原版 NeteaseCloudMusicApi 已停更；使用 Enhanced fork 仍有接口变更风险。需要把 API 地址做成配置，便于替换。
3. **Netease_url 的 Cookie 是服务端的**：设备登录 Cookie 不会自动传给 Netease_url。若走 `resolver`，需要在 `server/cookie.txt` 填同一账号的 VIP Cookie；否则默认走 `device`。
4. **无 PSRAM**：`audioI2S` 默认缓冲 704 KB 必须裁剪；FLAC/Hi-Res 可能内存不足，默认 `exhigh`。
5. **中文输入**：键盘只有 ASCII；v1 用热搜词 + 拼音/英文搜索，不承诺中文自由输入。
6. **TLS 内存**：HTTPS + 解码 + UI 同时进行风险高；优先局域网 HTTP，播放前完成下载。
7. **GPL-3.0**：裁剪版 `ESP32-audioI2S` 是 GPL-3.0，项目需按 GPL-3.0 分发。
8. **SD 卡质量**：SPI 25 MHz，劣质卡会掉速/写坏；下载使用 `.part` + 校验 + 重试。
9. **二维码尺寸**：135×135 显示网易云登录 URL，版本较高时模块较小；M2 实测扫码距离，必要时放大到全屏 QR 并分页显示提示。

---

## 13. 待确认项

1. **是否按本框架开始搭建？**
2. **音频后端默认值**：
   - `auto`：设备 API 优先，失败回退 Netease_url（推荐）
   - `device`：只用设备 Cookie，不部署 Netease_url
   - `resolver`：主要走 Netease_url 服务端 VIP 解析
3. **默认音质**：
   - `exhigh`（320 kbps，无 PSRAM 最稳，推荐）
   - `lossless`（FLAC，需 M1/M3 实测内存）
   - `standard`（128 kbps，最省资源）
4. **登录方式**：
   - 二维码 + SD `cookie.txt` 导入（推荐，最稳）
   - 仅二维码
   - 仅 `cookie.txt` 导入
5. **是否同时生成伴生服务的 `docker-compose.yml` 和部署说明？**（推荐是，否则固件无法独立工作）

确认后按 M1 → M6 顺序搭建，先做硬件 bring-up 和音频库验证，再接入账号与播放功能。
