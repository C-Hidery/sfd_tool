$ErrorActionPreference = "Stop"

# 在项目根目录下运行（PowerShell）：
#   .\scripts\release.ps1
#
# 功能：
# - 使用 CMake 生成/更新 Visual Studio 构建目录（build_vs）
# - 生成器默认使用 Visual Studio 2022 (x64)
# - 编译 Release 配置（不自动运行可执行文件）

$buildDir  = "build_vs"
$generator = "Visual Studio 17 2022"
$arch      = "x64"

function Update-I18N {
    param(
        [string]$Sources = "main.cpp GtkWidgetHelper.cpp pages/page_*.cpp ui_common.cpp"
    )

    if (-not (Get-Command xgettext -ErrorAction SilentlyContinue)) {
        Write-Host "[release][i18n] 未找到 xgettext，跳过 POT/PO 更新" -ForegroundColor Yellow
        return
    }

    Write-Host "[release][i18n] 更新 locale/sfd_tool.pot ..."
    & xgettext --language=C++ --keyword=_ --from-code=UTF-8 `
        --output=locale/sfd_tool.pot `
        $Sources.Split(" ")

    $python = $env:PYTHON
    if (-not $python) {
        if (Get-Command python3 -ErrorAction SilentlyContinue) {
            $python = "python3"
        } elseif (Get-Command python -ErrorAction SilentlyContinue) {
            $python = "python"
        }
    }

    if ($python) {
        Write-Host "[release][i18n] 同步 zh_CN sfd_tool.po ..."
        try {
            & $python "scripts/gen_po.py"
        } catch {
            Write-Host "[release][i18n] gen_po.py 执行失败，跳过本次翻译同步" -ForegroundColor Yellow
        }
    } else {
        Write-Host "[release][i18n] 未找到 python3/python，跳过 gen_po.py" -ForegroundColor Yellow
    }
}

Write-Host "[release] 检查 cmake ..."
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Host "[release] 错误：未找到 cmake 命令" -ForegroundColor Red
    Write-Host "  建议方式：" -ForegroundColor Yellow
    Write-Host "    1) 安装 Visual Studio 2022，并勾选 \"使用 C++ 的桌面开发\" 工作负载；" -ForegroundColor Yellow
    Write-Host "       然后在 \"x64 Native Tools Command Prompt for VS 2022\" 或 VS 开发者 PowerShell 中运行本脚本。" -ForegroundColor Yellow
    Write-Host "    2) 或使用 winget 安装 CMake：winget install Kitware.CMake" -ForegroundColor Yellow
    exit 1
}

# 在配置/构建前更新 POT/PO，避免漏翻
Update-I18N

Write-Host "[release] 使用生成器: $generator ($arch)"

Write-Host "[release] 配置 Release 构建 (VS)..."
cmake -S . -B $buildDir -G "$generator" -A $arch

Write-Host "[release] 编译 Release 构建..."
cmake --build $buildDir --config Release -- /m

$exePath = Join-Path $buildDir "Release/sfd_tool.exe"

Write-Host ""
Write-Host "[release] 构建完成：$exePath"
Write-Host "[release] 你可以在资源管理器中双击运行，或从命令行执行：" -ForegroundColor Yellow
Write-Host "  `"$exePath`""
