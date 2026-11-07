# CardpuerAdv-NeteaseMusicPlayer

基于 M5Stack Cardputer-Adv（ESP32-S3FN8，8 MB Flash，无 PSRAM）的独立网易云音乐播放器。
无需手机或电脑，设备自行完成扫码登录、搜索、歌单/收藏/FM 播放与歌词显示。

## 功能

- 二维码登录（网易云 App 扫码），Cookie 加密持久化，重启自动恢复
- 搜索：歌曲 / 歌手 / 专辑 / 歌单；歌手与专辑可直接进入歌曲列表
- 播放：我的歌单、我的收藏、喜欢/取消喜欢、私人 FM、红心模式、每日推荐、最近播放
- 播放控制：播放/暂停、上一首/下一首、音量、播放进度、LRC 歌词同步
- 播放源可切换：**SD 缓存**（先下载整首到 SD 再解码）或 **流式**（边下边播）
- 播放队列、顺序/列表循环/单曲循环/随机
- 音质选择：标准 / 较高 / 极高（非 MP3 自动回退标准音质，因固件仅内置 MP3 解码）
- 断网提示、会话过期重新登录、-462 风控自动退避重试

## 硬件与引脚

| 模块 | 引脚 |
| --- | --- |
| 显示 ST7789V2 240×135 | M5Unified 内部配置 |
| 键盘（TCA8418） | I2C SDA 8 / SCL 9，INT 11 |
| microSD | CS 12 / MOSI 14 / SCK 40 / MISO 39 |
| ES8311 + NS4150B | I2S BCLK 41 / LRCK 43 / DOUT 46 / DIN 42 |

## 构建

依赖全部随仓库提供（`lib/` 下 vendored），无需联网下载库：

```bash
pio run
```

- PlatformIO `espressif32@6.7.0`
- board `esp32-s3-devkitc-1`，8 MB Flash，分区表 `default_8MB.csv`
- Arduino ESP32 core 2.0.16，**不使用 PSRAM**

编译产物：`.pio/build/m5stack-cardputer-adv/firmware.bin`

## 烧录

USB-C 连接 Cardputer-Adv 后：

```bash
pio run -t upload
```

若设备未自动进入下载模式，按住 G0 再复位。烧录后打开串口（115200）可查看日志。

## 首次使用

1. 开机输入 2.4 GHz Wi-Fi SSID 与密码（保存在 NVS）。
2. 联网后自动进入二维码登录，用网易云 App 扫码确认。
3. 登录成功后进入主菜单：搜索 / 我的歌单 / 私人 FM / 红心模式 / 每日推荐 / 最近播放 / 我的收藏 / 账户 / 网络 / 设置。

### 按键

| 按键 | 功能 |
| --- | --- |
| `;` / `.` | 上移 / 下移 |
| `,` / `/` | 左 / 右（设置页调整数值、搜索页切换类型） |
| Enter | 确认 |
| Tab | 返回 / 退格（输入状态） |
| Space | 播放 / 暂停 |
| `n` / `p` | 下一首 / 上一首 |
| `l` | 喜欢 / 取消喜欢 |
| `q` | 播放器中打开队列 |

## 设置项

- 播放源：SD 缓存 / 流式（立即生效，作用于下一首）
- 音质：标准 / 较高 / 极高
- 音量、缓冲大小（32/48/64/96 KB）、播放模式

以上设置均写入 NVS，重启保留。

## 存储布局

```
/netease/cookie.enc     登录 Cookie（AES-256-CBC + HMAC-SHA256，密钥由 eFuse MAC 派生）
/netease/cache/<id>.mp3 歌曲缓存（最多保留 4 首，自动清理）
```

## 网络与安全

- API：`music.163.com`（普通表单）与 `interface3.music.163.com`（EAPI，AES-128-ECB）
- TLS 使用内嵌根证书校验（`include/TrustRoots.h`），不使用 `setInsecure()`
- 所有请求均设置连接/读取超时与响应大小上限
- Cookie 不以明文写入 SD，也不在日志中输出

## 已知限制

- 不支持拖动进度（MP3 解码器无随机定位）
- Wi-Fi 密码以明文保存在 NVS
- 仅解码 MP3；极高/较高音质若返回 FLAC 会自动回落标准音质
- 流式播放依赖网络质量；弱网建议使用 SD 缓存模式

## 接口可行性验证（2026-11-07）

使用与固件相同的 EAPI 算法（AES-128-ECB + MD5 digest）实测：

| 接口 | 结果 |
| --- | --- |
| `/api/login/qrcode/unikey` | 200，返回 unikey |
| `/api/cloudsearch/pc`（歌曲/歌手/歌单） | 200，字段解析正常 |
| `/api/song/lyric` | 200，LRC 正常 |
| `/api/v6/playlist/detail` | 200，200 首曲目 |
| `/api/v1/artist/songs` | 200，歌手歌曲列表正常 |
| `/api/v1/album/<id>` | 200，专辑歌曲正常 |
| `/api/song/enhance/player/url/v1`（未登录） | 200，url 为空（需 MUSIC_U） |
| TLS 证书链 | `*.music.163.com` / `*.music.126.net` 根证书为 DigiCert Global Root G2，已内嵌 |

高频连续请求会偶发 `-462` 风控，固件已实现 3 次退避重试。

## 状态说明

- 已完成：全部功能代码、PlatformIO 编译通过（RAM ≈ 17.6%，Flash ≈ 48.6%）
- 待实机验收：扫码登录、音频解码出声、实机 TLS 握手、SD 并发、长时间播放稳定性
