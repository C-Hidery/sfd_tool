# SFD Tool 自动化任务包指南

> 相关文档： [README_ZH](../README_ZH.md) · [操作手册](USER_GUIDE_ZH.md) ·
> [CMake 使用指南](cmake.md) · [版本记录](VERSION_LOG.md)

自动化任务页用于加载 ZIP 自动化包，检查设备分区兼容性，并按
`package.json` 中 `actions` 的顺序执行命令。

> **高风险提示：** 自动化包可以执行分区写入、擦除、重新分区、
> AVB/TrustOS 修改、重启等操作。错误的自动化任务或损坏的镜像
> 可能导致设备变砖或数据永久丢失。请确认包来源可信，并提前备份
> 关键分区。

---

## 1. 页面组成

- 左侧：包内分区列表
  - 序号 + 分区名；
  - 包内声明大小；
  - 原始分区名/类型；
  - 设备实际分区大小。
- 右侧：
  - **Package File Path**：选择 ZIP、加载包；
  - **Package Info**：包名、作者、版本、联系方式、类型；
  - **Task Selection**：按 `actions` 顺序显示任务，默认全部勾选；
  - **Re-check Package**：重新检查包并解锁 `Start`；
  - **Start**：执行选中的任务。
- 底部 **Log**：显示命令、`print` 输出、执行状态和错误信息。

---

## 2. package.json 格式

ZIP 根目录必须包含 `package.json`：

```json
{
  "name": "example",
  "author": "Ryan",
  "version": "0.0.0",
  "contact": "ryan110413@outlook.com",
  "type": "single_act",
  "actions": [
    "repartition"
  ],
  "partitions": [
    ["boot", "35"],
    ["recovery", "35"],
    ["userdata", "0xffffffff"]
  ]
}
```

字段说明：

| 字段 | 必填 | 说明 |
| --- | --- | --- |
| `name` | 否 | 包名，可为空 |
| `author` | 否 | 作者，可为空 |
| `version` | 否 | 版本，可为空 |
| `contact` | 否 | 联系方式，可为空 |
| `type` | 是 | 只能是 `write_part` 或 `single_act` |
| `actions` | 是 | 非空字符串数组，每个元素是一条命令 |
| `partitions` | 否 | 分区表，元素格式为 `[分区名, 大小]` |

约束：

- `type` 只能有一个值，其他值或同时出现多个值会被拒绝；
- `write_part` 类型要求 ZIP 根目录存在 `images/` 文件夹；
- `partitions` 可选，存在时用于兼容性检查和 `repartition`；
- 分区大小默认单位为 **MB**，也兼容 `35MB`、`35840KB` 等写法；
- `userdata` 的实际容量随设备变化，**只检查是否存在，不比较大小**；
- `userdata` 可使用 `0xffffffff` 表示“剩余空间”，`repartition`
  时会原样写入；
- `partitions` 中不允许出现 `splloader`，否则整个包会被拒绝。

---

## 3. 加载与兼容性检查

1. 点击 `...` 选择 ZIP 包，再点击 `Load`；
2. 选择解压目录；
3. 工具读取 `package.json`，校验包类型、`actions`、分区表和 `images/`；
4. 若包内有 `partitions`，逐个检查设备分区：
   - 分区不存在 → 失败；
   - 分区存在但大小不匹配 → 失败；
   - `userdata` 只检查存在性；
5. 检查失败时：
   - 弹窗提示，并复用 PAC 分区表比较弹窗展示差异；
   - 拒绝使用该包，`Start` 保持禁用；
6. 设备未连接或分区表为空时：
   - 警告“无法验证兼容性”；
   - `Start` 保持禁用；
7. 设备状态变化后，可点击 **Re-check Package** 重新检查并解锁
   `Start`。

---

## 4. 执行命令

点击 `Start` 后会先显示安全确认警告，确认后按 `Task Selection`
中的顺序执行。任意一条命令失败都会立即终止，并弹窗显示失败原因。

- 命令执行和输出显示在页面底部 Log；
- `reset`、`poweroff`、`reboot-fastboot`、`reboot-recovery`
  成功后会断开设备，工具会禁用所有 GUI 按钮，并提示重新连接设备、
  重启工具；
- 执行结束后 `Start` 不会自动解锁，需要重新检查包后再使用。

---

## 5. 命令列表

| 命令 | 说明 |
| --- | --- |
| `write_part name image` | 将 `images/` 下的镜像写入指定分区 |
| `read_part name path` | 读取分区到指定文件 |
| `erase_part name` | 擦除指定分区 |
| `blk_size size` | 设置块大小，最大 `0xF800` |
| `repartition` | 使用 `package.json` 的 `partitions` 重新分区 |
| `write_parts` | 遍历 `images/`，按文件名批量写入分区 |
| `dis_avb_tos path` | 备份并修改 TrustOS 以关闭 AVB |
| `e_bl path` | 备份 `miscdata` 并设置 bootloader 状态 |
| `verity 0\|1` | 关闭/启用 dm-verity |
| `slot 0\|1\|2` | 选择 A/B slot |
| `rawdata 0\|1\|2` | 设置 rawdata 模式 |
| `reset` | 重启设备 |
| `poweroff` | 关机 |
| `reboot-fastboot` | 重启到 fastboot |
| `reboot-recovery` | 重启到 recovery |
| `set_active a\|b` | 设置活动 slot |
| `timeout value` | 设置设备超时时间（毫秒） |
| `print TEXT` | 在底部日志输出文本 |

`write_part` 和 `write_parts` 的镜像路径根目录是解压目录下的
`images/`。`read_part`、`dis_avb_tos`、`e_bl` 的路径参数按命令中
给出的路径使用。

---

## 6. 安全提示

- 不要在未确认包内容的情况下点击 `Start`；
- 错误的自动化任务、损坏的镜像或不匹配的分区表都可能导致设备损坏；
- 执行涉及 `dis_avb_tos`、`e_bl`、`verity`、`repartition` 等操作前，
  请确保已具备恢复手段；
- 因使用自动化包导致的设备损坏由用户自行承担。
