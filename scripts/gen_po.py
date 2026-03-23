import os

translation_dict = {
    "Connect": "连接",
    "Partition Operation": "分区操作",
    "Manually Operate": "手动操作",
    "Advanced Operation": "高级操作",
    "Advanced Settings": "高级设置",
    "Debug Options": "调试设置",
    "About": "关于",
    "Log": "日志",
    "Export": "导出",
    "Clear": "清空",
    "Progress:": "进度:",
    "POWEROFF": "关机",
    "REBOOT": "重启",
    "BOOT TO RECOVERY": "重启到恢复模式",
    "BOOT TO FASTBOOT": "重启到线刷模式",
    "Execute": "执行",
    "Advanced": "高级选项",
    "CONNECT": "连接",
    "Cancel": "取消",
    "Modify Partition Table": "修改分区表",
    "FDL Send Settings": "FDL发送设置",
    "FDL File Path :": "FDL文件路径 :",
    "FDL Send Address :": "FDL发送地址 :",
    "Welcome to SFD Tool GUI!": "欢迎使用SFD Tool GUI!",
    "Please connect your device with BROM mode": "请将你的设备连接到BROM模式",
    "Press and hold the volume up or down keys and the power key to connect": "按住音量增大或减小键和电源键进行连接",
    "Wait connection time (s):": "连接等待时间 (s):",
    "Try to use CVE to skip FDL verification": "利用漏洞绕过FDL签名验证",
    "Kick device to SPRD4": "使用SPRD4模式",
    "Kick One-time Mode": "Kick单次模式",
    "CVE Binary File Address": "CVE可执行镜像",
    "CVE Addr": "CVE镜像地址",
    "Enable CVE v2": "启用CVE v2",
    "Warning": "警告",
    "Error": "错误",
    "Program Crash": "程序崩溃",
    "Confirm": "确认",
    "Completed": "完成",
    "You are running this tool without root permission!\\nIt may cause device connecting issue\\nRecommanded to open this tool with root permission!\\n\\nsudo -E /path/to/sfd_tool": "检测到未使用ROOT权限运行此工具\\n可能会引起设备连接问题\\n建议在ROOT环境下运行此工具\\n\\nsudo -E /path/to/sfd_tool",
    "The program encountered an unhandled exception, which may be caused by device connection issues or a bug in the program.\\n\\nIt is recommended to check the device connection, ensure the correct options are used, and try running the tool again.": "程序发生了未处理的异常，可能是由于设备连接问题或程序错误引起的。\\n\\n建议检查设备连接，确保使用了正确的选项，并尝试重新运行工具。",
    "Device unattached, exiting...": "设备已断开连接！正在退出...",
    "No partition list file selected!": "未选择分区列表文件！",
    "No partition table loaded, cannot write partition list!": "当前未加载分区表，无法写入分区列表！",
    "Partition write completed!": "分区写入完成！",
    "Force writing partitions may brick the device, do you want to continue?": "强制写入分区可能会导致设备变砖，是否继续？",
    "Force write mode does not allow writing to splloader partition!": "强制写入模式下不允许写入splloader分区！",
    "Currently in compatibility-method-PartList mode, force writing may brick the device!": "当前处于兼容分区表模式，强制写入可能会导致设备变砖！",
    "Currently in compatibility-method-PartList mode, modifying partition may brick the device!": "当前处于兼容分区表模式，修改分区可能会导致设备变砖！",
    "Partition force write completed!": "分区强制写入完成！",
    "No save path selected!": "未选择保存路径！",
    "Partition read completed!": "分区读取完成！",
    "Partition erase completed!": "分区擦除完成！",
    "No partition table loaded, cannot restore from folder!": "当前设备尚未加载分区表，请先读取分区列表后再尝试从文件夹恢复。",
    "No partition table loaded, cannot modify partition size!": "当前未加载分区表，无法修改分区大小！",
    "Please fill in complete modification info!": "请填写完整的修改信息！",
    "Please enter a valid new size!": "请输入合法的新大小！",
    "Partition does not exist!": "欲修改分区不存在！",
    "Second partition does not exist!": "第二分区不存在！",
    "Failed to parse modified partition table!": "解析修改后的分区表失败！",
    "Partition modification completed!": "分区修改完成！",
    "Partition already exists!": "分区已存在！",
    "Partition after does not exist!": "后一个指定的分区不存在！",
    "Status : ": "状态：",
    "   Mode : ": "   模式：",
    "Storage Type": "存储类型",
    "Unknown": "未知",
    "Slot": "槽位",
    "Ready": "准备就绪",
    "Writing partition": "写入分区",
    "Force Writing partition": "强制写入分区",
    "Reading partition": "读取分区",
    "Erase partition": "擦除分区",
    "Modify partition table": "修改分区表",
    "Check Backup File": "效验备份文件",
    "Backup Partition": "执行备份",
    "List": "列表",
    "List Write": "写入分区列表",
    "List Read": "读取列表中分区",
    "List Force Write": "强制写入分区列表",
    "List Erase": "列表中的分区擦除",
    "Modify Add :": "修改增加 :",
    "Modify Del": "删除指定的分区",
    "Modify Ren": "重命名指定分区",
    "Modify SetAB": "自动设置AB分区(需手动提供大小)",
    "Set To A": "自动设置为a槽位",
    "Set To B": "自动设置为b槽位",
    "List Cancel": "取消列表操作",
    "M-Write": "手动写入",
    "M-Read": "手动读取",
    "M-Erase": "手动擦除",
    "M-Cancel": "取消手动操作",
    "Start Repart": "开始重新分区",
    "Backup All": "备份全部分区",
    "Set Active A": "设置活动槽位为 A",
    "Set Active B": "设置活动槽位为 B",
    "Chip UID": "获取芯片 UID",
    "Pac Time": "获取 PAC 编译时间",
    "Check Nand": "检查 Nand 信息",
    "Disable AVB(Only for some devices)": "关闭 AVB 校验(仅支持部分设备)",
    "Modify Part(Shrink 2nd)": "修改分区(第二分区缩容)",
    "Modify New": "添加新分区",
    "Force Format": "强制格式化",
    "Get XML": "获取设备分区表XML",
    "Blk Size:": "块大小:",
    "End Data Settings:": "End Data设置:",
    "Transcode Settings:": "转码设置:",
    "Raw Data(Nand) Settings:": "Raw Data(Nand)设置:",
    "Charge Settings:": "充电设置:",
    "Enable": "启用",
    "Disable": "禁用",
    "Select File": "选择文件",
    "Dump Memory(Before FDL)": "导出内存(需在执行FDL前)",
    "Address:": "地址:",
    "Size:": "大小:",
    "Export Log": "导出日志",
    "Clear Log": "清空日志",
    "Select file": "选择文件",
    "Select folder": "选择文件夹",
    "Saving files": "保存文件",
    "All files (*.*)": "所有文件 (*.*)",
    "Data block size": "数据块大小",
    "Enable Rawdata mode": "启用Rawdata模式",
    "Enable transcode - FDL1": "启用转码 --- FDL1",
    "Disable transcode": "禁用转码",
    "Disable Rawdata mode": "禁用Rawdata模式",
    "Tips": "提示",
    "Info": "提示",
    "Successfully connected": "设备连接成功",
    "About sfd_tool\\nThis program is a command-line flashing tool based on Linux and Mac, the original repository has been archived. This is a version with GUI support.": "关于sfd_tool程序\\n此程序是一个基于Linux和Mac的命令行刷机工具，原版仓库已经存档。这是带GUI支持的版本。",
    "Debug Options Page\\nThis page contains device debugging functions\\n": "调试选项页面\\n此页面包含设备调试功能\\n",
    "Get partition table through scanning an Xml file": "获取分区表 XML",
    "This operation may break your device, and not all devices support this, if your device is broken, flash backup in backup_tos, continue?": "此操作可能会使你的设备损坏，并且不是所有设备都支持此操作，如果设备损坏，请刷回backup_tos里的备份，是否继续？",
    "Disabled AVB successfully, the backup trustos is tos_bak.bin": "禁用AVB成功，原版trustos是tos_bak.bin",
    "Disabled AVB failed, go to console window to see why": "禁用AVB失败，请检查控制台窗口输出内容",
    "Successfully enabled raw data mode": "启用RawData模式成功",
    "Failed to enable raw data mode, please set value!": "启用RawData模式失败，请设置数值！",
    "Successfully disabled raw data mode": "禁用RawData模式成功",
    "Enabled transcode successfully": "启用转码成功",
    "Disabled transcode successfully": "禁用转码成功",
    "Set successfully": "设置成功",
    "No partition table found on current device, read partition list through compatibility method?\\nWarn: This mode may not find all partitions on your device, use caution with force write or editing partition table!": "当前设备未找到分区表，是否通过兼容方式读取分区列表？\\n警告：此模式可能无法找到设备上的所有分区，强制写入或修改分区表时请谨慎使用！",
    "You have entered Reconnect Mode, which only supports compatibility-method partition list retrieval, and [storage mode/slot mode] can not be gotten!": "你已启动重连模式，重连模式下只能兼容获取分区列表，且不能获取槽位和储存类型！",
    "Device already connected! Some advanced settings opened!": "设备已成功连接！部分高级设置已开放！",
    "Please execute FDL file to continue!": "请执行FDL以继续！",
    "Since your device is in SPRD4 mode, you can choose to skip FDL setting and directly execute FDL, but not all devices support that, please proceed with caution!": "由于你的设备处于SPRD4模式，你可以选择跳过FDL设置直接执行FDL，但是不是所有设备都支持，请谨慎操作！",
    "Device can be booted without FDL in SPRD4 mode, continue?": "设备在SPRD4模式下可以无需FDL启动，是否继续？",
    "FDL2 executed successfully!": "FDL2已成功执行！",
    "Do not set block size manually in high speed mode!": "高速模式下请勿手动设置块大小！",
    "FDL1 executed successfully!": "FDL1已成功执行！",
    "Current partition operation cancelled!": "已取消当前分区操作！",
    "Partition table not available!": "分区表不可用！",
    "No partition image file selected!": "未选择分区镜像文件！",
    "No partition name specified!": "未指定分区名称！",
    "Device is not using VAB!": "设备不是VAB分区表！",
    "Current active partition set to Slot A!": "已设置当前分区为A槽！",
    "Current active partition set to Slot B!": "已设置当前分区为B槽！",
    "Repartition completed!": "重新分区完成！",
    "Partition table export completed!": "分区表导出完成！",
    "DM-Verity and AVB protection enabled!": "已启用DM-Verity和AVB保护！",
    "DM-Verity and AVB protection disabled!": "已禁用DM-Verity和AVB保护！",
    "Failed to save log file!": "无法保存日志文件！",
    "Log export completed!": "日志导出完成！",
    "Please check a partition": "请选择一个分区",
    "Operation": "操作",
    "WRITE": "刷写",
    "FORCE WRITE": "强制刷写",
    "EXTRACT": "读取分区",
    "ERASE": "擦除分区",
    "Backup All": "备份全部分区",
    "Get partition table through scanning an Xml file": "通过XML文件获取分区列表",
    "[Change size] Please check a partition you want to change": "欲修改大小：请选择一个你想修改大小的分区",
    "Second-change partition": "连带受影响第二分区",
    "New size in MB": "新大小（MB）",
    "Modify": "修改",
    "[Add partition] Enter partition name:": "欲添加分区：请输入分区名称：",
    "Part name:": "分区名：",
    "Size(MB):": "大小（MB）：",
    "Part after this new part:": "新分区放置在该分区之前：",
    "[Remove partition] Please check a partition you want to remove": "欲移除分区：请选择一个你想要移除的分区",
    "[Rename partition] Please check a partition you want to rename": "欲重命名分区：请选择一个你想重命名的分区",
    "New name": "新名称",
    "Write partition": "刷写分区 (Write)",
    "Partition name:": "分区名：",
    "Image file path:": "镜像文件路径：",
    "Extract partition": "读取分区 (Extract)",
    "Erase partition": "擦除分区 (Erase)",
    "Toggle the A/B partition boot settings": "切换A/B分区启动设置",
    "Boot A partitons": "启动A分区",
    "Boot B partitions": "启动B分区",
    "Repartition": "重新分区",
    "XML part info file path": "XML分区表文件路径",
    "START": "开始",
    "Extract part info to a XML file (if support)": "备份分区表到XML文件（如果支持）",
    "DM-verity and AVB Settings (if support)": "DM验证和AVB设置（如果支持）",
    "Disable DM-verity and AVB": "禁用DM-verity和AVB (Android 10+)",
    "Enable DM-verity and AVB": "启用DM-verity和AVB (Android 10+)",
    "Trustos AVB Settings": "TrustOS AVB设置",
    "[CAUTION] Disable AVB verification by patching trustos(Android 9 and lower)": "【警告】修补trustos以禁用AVB校验 (Android 9及以下)",
    "Rawdata Mode --- Value support: {1, 2}": "Rawdata模式支持数值：{1, 2}",
    "Value:": "数值：",
    "Transcode --- FDL1/2": "转码 --- FDL1/2",
    "Disable transcode --- FDL2": "禁用转码 --- FDL2",
    "Charging Mode --- BROM": "充电模式 --- BROM",
    "Enable Charging mode --- BROM": "启用充电模式 --- BROM",
    "Disable Charging mode --- BROM": "禁用充电模式 --- BROM",
    "Send End Data": "发送结束块数据",
    "Enable sending end data": "开启发送End Data",
    "Disable sending end data": "关闭发送End Data",
    "Operation timeout": "操作超时重试倒计时",
    "A/B Part read/flash manually set --- FDL2": "A/B分区刷写手工干预 --- FDL2",
    "Not VAB --- FDL2": "非A/B分区 (传统) --- FDL2",
    "A Parts --- FDL2": "仅操作A分区组 --- FDL2",
    "B Parts --- FDL2": "仅操作B分区组 --- FDL2",
    "Get pactime": "读取PAC烧录时间记录",
    "Get chip UID": "获取芯片硬件UID",
    "Check if NAND Storage": "检查NAND存储标识",
    "Debug Options Page\\nThis page contains device debugging functions": "调试选项页面\\n此页面包含设备调试功能",
    "_Cancel": "_取消",
    "_Open": "_打开",
    "Save file": "保存文件",
    "_Save": "_保存",
    "_Select": "_选择",
    "Tips": "提示",
    "Info": "信息",
    "Confirm": "确认",
    "Successfully connected": "连接成功",
    "FDL2 Executed": "FDL2执行成功",
    "High Speed Mode Enabled": "高速模式已启用",
    "FDL1 Executed": "FDL1执行成功",
    "An error occurred. The application will now exit.": "监测到错误，应用程序将退出。",
    "XML files (*.xml)": "XML文件 (*.xml)",
    "Text files (*.txt)": "文本文件 (*.txt)",
    "All files (*.*)": "所有文件 (*.*)"
}

pot_path = "locale/sfd_tool.pot"
po_path = "locale/zh_CN/LC_MESSAGES/sfd_tool.po"


def _extract_msgids(path):
    """Return list of msgid strings (including header msgid "").

    注意：只收集 msgid 及其续行，一旦遇到 msgstr 即停止，
    避免把 header 的 msgstr 行误拼进 msgid。
    """
    msgids = []
    current = None
    collecting = False
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            if line.startswith("msgid "):
                # 结束上一个 msgid
                if current is not None:
                    msgids.append(current)
                raw = line[6:].strip()
                if raw.startswith('"'):
                    current = raw.strip().strip('"')
                    collecting = True
                else:
                    current = ""
                    collecting = False
            elif collecting:
                if line.startswith("msgstr"):
                    # 到达 msgstr 段，停止拼接 msgid
                    collecting = False
                elif line.startswith('"'):
                    # msgid 续行
                    current += line.strip().strip('"')
        # 文件末尾收尾
        if current is not None:
            msgids.append(current)
    return msgids


def _escape_for_po(text: str) -> str:
    """Escape string for inclusion in a PO file."""
    return (
        text.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
    )


def main() -> None:
    if not os.path.exists(pot_path):
        raise SystemExit(f"POT file not found: {pot_path}")
    if not os.path.exists(po_path):
        raise SystemExit(f"PO file not found: {po_path}")

    pot_ids = [mid for mid in _extract_msgids(pot_path) if mid != ""]
    po_ids = set(mid for mid in _extract_msgids(po_path) if mid != "")

    missing_ids = [mid for mid in pot_ids if mid not in po_ids]

    if not missing_ids:
        print("No new msgid found; PO is up to date.")
        return

    auto_filled = []
    untranslated = []

    with open(po_path, "a", encoding="utf-8") as f:
        f.write("\n\n# ===== Auto-appended entries from sfd_tool.pot =====\n")
        for mid in missing_ids:
            if mid in translation_dict:
                translation = translation_dict[mid]
                auto_filled.append(mid)
            else:
                # English fallback, needs real translation later
                translation = mid
                untranslated.append(mid)

            f.write("\n")
            f.write(f'msgid "{_escape_for_po(mid)}"\n')
            f.write(f'msgstr "{_escape_for_po(translation)}"\n')

    print(f"Total msgid in POT (excluding header): {len(pot_ids)}")
    print(f"Existing msgid in PO: {len(po_ids)}")
    print(f"New entries appended: {len(missing_ids)}")
    print(f"  Auto-filled with translation_dict: {len(auto_filled)}")
    print(f"  Still untranslated (using English fallback): {len(untranslated)}")
    if untranslated:
        print("\nUntranslated msgid (need manual Chinese translation):")
        for mid in untranslated:
            print(f"- {mid}")


if __name__ == "__main__":
    main()
