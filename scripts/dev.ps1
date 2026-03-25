$ErrorActionPreference = "Stop"

# 在项目根目录下运行（PowerShell）：
#   .\scripts\dev.ps1
#
# 功能：
# - 使用 CMake 生成/更新 Visual Studio 构建目录（build_vs）
# - 生成器默认使用 Visual Studio 2022 (x64)
# - 编译 Debug 配置
# - 如果可执行文件存在，则自动启动 sfd_tool（Debug）

$buildDir  = "build_vs"
$generator = "Visual Studio 17 2022"
$arch      = "x64"

function Update-I18N {
    param(
        [string]$Sources = "main.cpp GtkWidgetHelper.cpp pages/page_*.cpp ui/ui_common.cpp"
    )

    if (-not (Get-Command xgettext -ErrorAction SilentlyContinue)) {
        Write-Host "[dev][i18n] 未找到 xgettext，跳过 POT/PO 更新" -ForegroundColor Yellow
        return
    }

    Write-Host "[dev][i18n] 更新 locale/sfd_tool.pot ..."
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
        Write-Host "[dev][i18n] 同步 zh_CN sfd_tool.po ..."
        try {
            & $python "scripts/gen_po.py"
        } catch {
            Write-Host "[dev][i18n] gen_po.py 执行失败，跳过本次翻译同步" -ForegroundColor Yellow
        }
    } else {
        Write-Host "[dev][i18n] 未找到 python3/python，跳过 gen_po.py" -ForegroundColor Yellow
    }
}

Write-Host "[dev] 检查 cmake ..."
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Host "[dev] 错误：未找到 cmake 命令" -ForegroundColor Red
    Write-Host "  建议方式：" -ForegroundColor Yellow
    Write-Host "    1) 安装 Visual Studio 2022，并勾选 \"使用 C++ 的桌面开发\" 工作负载；" -ForegroundColor Yellow
    Write-Host "       然后在 \"x64 Native Tools Command Prompt for VS 2022\" 或 VS 开发者 PowerShell 中运行本脚本。" -ForegroundColor Yellow
    Write-Host "    2) 或使用 winget 安装 CMake：winget install Kitware.CMake" -ForegroundColor Yellow
    exit 1
}

# 在配置/构建前更新 POT/PO，避免漏翻
Update-I18N

Write-Host "[dev] 使用生成器: $generator ($arch)"

Write-Host "[dev] 配置 Debug 构建 (VS)..."
cmake -S . -B $buildDir -G "$generator" -A $arch `
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

Write-Host "[dev] 编译 Debug 构建..."
cmake --build $buildDir --config Debug -- /m

$exePath = Join-Path $buildDir "Debug/sfd_tool.exe"

if (Test-Path $exePath) {
    Write-Host "[dev] 启动 sfd_tool (Debug) ..."
    Start-Process $exePath
} else {
    Write-Host "[dev] 提示：未在预期位置找到可执行文件，请检查：" -ForegroundColor Yellow
    Write-Host "  $exePath"
}
