# SFD Tool Automated Task Packages

> Related documents: [README](../README.md) · [User Guide (ZH)](USER_GUIDE_ZH.md) ·
> [CMake Guide](cmake_EN.md) · [Version Log](VERSION_LOG.md)

The Automated Task page loads a ZIP automation package, validates its
partition table against the connected device, and executes the commands
listed in `package.json` in order.

> **High-risk warning:** automation packages can write or erase partitions,
> repartition the device, modify AVB/TrustOS, reboot, and perform other
> dangerous operations. A wrong package or corrupted image may brick the
> device or cause permanent data loss. Only use packages from a trusted
> source and always keep backups of critical partitions.

---

## 1. Page Layout

- Left side: package partition list
  - index + partition name;
  - declared package size;
  - raw partition name/type;
  - actual device partition size.
- Right side:
  - **Package File Path**: choose a ZIP and load it;
  - **Package Info**: name, author, version, contact, and type;
  - **Task Selection**: commands from `actions`, checked by default;
  - **Re-check Package**: re-run validation and unlock `Start`;
  - **Start**: execute the selected tasks.
- Bottom **Log**: command output, `print` text, status, and errors.

---

## 2. package.json Format

The ZIP root must contain a `package.json`:

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

Fields:

| Field | Required | Description |
| --- | --- | --- |
| `name` | No | Package name; may be empty |
| `author` | No | Author; may be empty |
| `version` | No | Version; may be empty |
| `contact` | No | Contact; may be empty |
| `type` | Yes | `write_part`, `single_act`, `fdl_exec`, or `fdl_exec_wpart` |
| `actions` | Yes | Non-empty array of command strings |
| `partitions` | No | Array of `[partition_name, size]` entries |

Constraints:

- `type` must have exactly one value; other values are rejected;
- `write_part` requires an `images/` folder in the ZIP root;
- `fdl_exec` requires an `fdl/` folder and at least one `fdl` command;
- `fdl_exec_wpart` requires both `fdl/` and `images/` folders in the ZIP
  root, plus at least one `fdl` command;
- `partitions` is optional and is used for compatibility checks and
  `repartition`;
- sizes are interpreted as **MB** by default; `35MB` and `35840KB`
  are also accepted;
- `userdata` only needs to exist on the device; its size is **not**
  compared;
- `userdata` may use `0xffffffff` to mean “remaining space”; it is
  written back as-is by `repartition`;
- `splloader` must not appear in `partitions`; otherwise the package is
  rejected.

---

## 3. Loading and Compatibility Checks

1. Click `...` to choose a ZIP package, then click `Load`;
2. Choose an extraction directory;
3. The tool reads `package.json` and validates the package type,
   `actions`, partition table, and `images/` folder;
4. For `fdl_exec` / `fdl_exec_wpart`, compatibility is deferred until
   `Start` has executed the `fdl` / `exec` / `cptable` preparation commands;
5. If `partitions` is present, each partition is checked against the device:
   - missing partition → failure;
   - size mismatch → failure;
   - `userdata` is checked for existence only;
6. On failure:
   - a warning is shown and the PAC-style partition comparison dialog is
     reused to display differences;
   - the package is rejected and `Start` stays disabled;
7. If the device is not connected or the partition table is empty:
   - a warning explains that compatibility cannot be verified;
   - `Start` stays disabled;
8. After the device state changes, click **Re-check Package** to validate
   again and unlock `Start`.

---

## 4. Executing Commands

Clicking `Start` first shows a safety confirmation. After confirmation,
commands run in the order of **Task Selection**. The first failure stops
execution and shows a warning dialog.

- For `fdl_exec` / `fdl_exec_wpart`, the `fdl`, `exec`, and optional
  `cptable` preparation commands run before the partition compatibility
  check; incompatible packages stop before remaining actions;
- `wait TIME` pauses execution for TIME seconds;
- `pause message` shows an Info dialog with the message and waits for
  confirmation before continuing;
- Partition read/write/erase, repartition, `dis_avb_tos`, `e_bl`,
  `verity`, `set_active`, and `cptable` require the device to be in
  **FDL2** stage; otherwise execution stops with an error;
- Command output appears in the bottom Log pane;
- the main window status bar shows the current command, device mode
  (BROM/FDL1/FDL2), and progress; it updates on failure or disconnect;
- `reset`, `poweroff`, `reboot-fastboot`, and `reboot-recovery`
  disconnect the device on success; all GUI buttons are then disabled
  and the user is asked to reconnect and restart the tool;
- `Start` is not automatically re-enabled after execution. Run
  **Re-check Package** before starting again.

---

## 5. Command Reference

| Command | Description |
| --- | --- |
| `write_part name image` | Write an image from `images/` to a partition |
| `read_part name path` | Read a partition to a file |
| `erase_part name` | Erase a partition |
| `blk_size size` | Set block size (max `0xF800`) |
| `repartition` | Repartition using `package.json` `partitions` |
| `write_parts` | Write all files in `images/` by filename |
| `dis_avb_tos path` | Back up and patch TrustOS to disable AVB |
| `e_bl path` | Back up `miscdata` and set bootloader state |
| `verity 0\|1` | Disable/enable dm-verity |
| `slot 0\|1\|2` | Select A/B slot |
| `rawdata 0\|1\|2` | Set rawdata mode |
| `reset` | Reboot the device |
| `poweroff` | Power off the device |
| `reboot-fastboot` | Reboot to fastboot |
| `reboot-recovery` | Reboot to recovery |
| `set_active a\|b` | Set the active slot |
| `timeout value` | Set device timeout in milliseconds |
| `wait TIME` | Wait TIME seconds before continuing |
| `pause message` | Show an Info dialog and wait for confirmation |
| `fdl FILE addr` | Load an FDL file relative to the `fdl/` folder |
| `exec_addr FILE addr` | Set the BROM-stage helper file relative to `fdl/` |
| `exec [ADDR]` | Execute FDL1→FDL2 in FDL1 (no address); BROM stage requires an address |
| `send FILE addr` | Send a file relative to `fdl/` to an address |
| `cptable` | Read the partition table via compatibility mode |
| `print TEXT` | Print text to the bottom log |

`write_part` and `write_parts` resolve image paths relative to
`<extraction_dir>/images/`; `fdl_exec_wpart` also uses the root
`images/` folder.
`fdl`, `exec_addr`, and `send` resolve file paths relative to `fdl/`.
Paths for `read_part`, `dis_avb_tos`, and `e_bl` are used as provided.

---

## 6. Safety Notes

- Do not click `Start` until you have verified the package contents;
- a wrong automation package, corrupted image, or mismatched partition
  table can damage the device;
- before using `dis_avb_tos`, `e_bl`, `verity`, `repartition`, or similar
  operations, make sure you have a recovery path;
- device damage caused by using an automation package is the user's
  responsibility.
