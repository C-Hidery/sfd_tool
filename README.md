# sfd_tool

[**中文文档**](https://github.com/C-Hidery/sfd_tool/blob/master/README_ZH.md)

![Logo](icon.png)

![License](https://img.shields.io/github/license/C-Hidery/sfd_tool)

The modified version of tool 'spreadtrum_flash'

sfd_tool is a maintenance release for spreadtrum_flash

[spreadtrum_flash](https://github.com/TomKing062/spreadtrum_flash)

---

Run this before making on linux:

``` bash
sudo apt update
# Ubuntu/Obsidian
sudo apt install libusb-1.0-0-dev
# Android(Termux)
pkg install termux-api libusb clang git
```

Then make:
``` bash
make
```

Use on Termux:

``` bash
# Search OTG device
termux-usb -l
[
  "/dev/bus/usb/xxx/xxx"
]
# Authorize OTG device
termux-usb -r /dev/bus/usb/xxx/xxx
# Run
termux-usb -e './sfd_tool --usb-fd' /dev/bus/usb/xxx/xxx
```

---

***Modified commands:***

    part_table [FILE PATH]

**This command is equivalent to the `partition_list` command.**

    exec_addr [BINARY FILE] [ADDR]
    
**Modified, you need to provide file path and address**

    exec <ADDR>

**Modified, you need to provide FDL1 address when you execute FDL1**

    read_spec [PART NAME] [SIZE] [OFFSET]

**Modified, equivalent to the `read_part` command, then `read_part` is equivalent to the `r`**
