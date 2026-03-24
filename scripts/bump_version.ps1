$ErrorActionPreference = "Stop"

# bump_version.ps1 - 交互式更新 sfd_tool 版本号和相关文件，并提交 git
#
# 用法（在仓库根目录 PowerShell 中运行）：
#   .\scripts\bump_version.ps1

# 仓库根目录：脚本所在目录的上一层
$rootDir = Split-Path -LiteralPath $PSScriptRoot -Parent
Set-Location $rootDir

# 确保暂存区干净，避免把其他改动一起提交
& git diff --cached --quiet
if ($LASTEXITCODE -ne 0) {
    Write-Host "Error: 暂存区已有改动，请先提交或取消暂存 (git reset) 再运行本脚本。" -ForegroundColor Red
    exit 1
}

# 读取版本号（容错：自动去掉空白、中文句号等）
$newVersion = $null
while ($true) {
    $raw = Read-Host "请输入新版本号 (格式 X.Y.Z.W，例如 1.7.8.0，输入 q 退出)"

    if ($raw -eq "q" -or $raw -eq "Q") {
        Write-Host "已取消"
        exit 0
    }

    # 清理输入：去掉空白，将中文句号替换为英文句号，只保留数字和点
    $cleaned = $raw -replace '\s+', ''
    $cleaned = $cleaned -replace '。', '.'
    $cleanedChars = $cleaned.ToCharArray() | Where-Object { $_ -match '[0-9.]' }
    $newVersion = -join $cleanedChars

    if ($newVersion -match '^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$') {
        break
    }

    Write-Host "Error: 版本号格式必须类似 1.7.8.0，请重新输入。" -ForegroundColor Red
}

Write-Host ""
$logLine = Read-Host "请输入本次更新内容（用于 docs/VERSION_LOG.md，单行描述即可）"
if ([string]::IsNullOrWhiteSpace($logLine)) {
    $logLine = "No details provided."
}

# [1/4] 更新 VERSION.txt
Write-Host "[1/4] 更新 VERSION.txt..."
Set-Content -Path "VERSION.txt" -Value $newVersion -Encoding UTF8

# [2/4] 更新 docs/VERSION_LOG.md 头部版本 + 追加日志
if (Test-Path "docs/VERSION_LOG.md") {
    Write-Host "[2/4] 更新 docs/VERSION_LOG.md 头部版本..."
    $lines = Get-Content "docs/VERSION_LOG.md"

    # 替换第一处 "Version ... LTV Edition"
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^Version .* LTV Edition$') {
            $lines[$i] = "Version $newVersion LTV Edition"
            break
        }
    }

    Write-Host "[3/4] 追加本次版本日志到 docs/VERSION_LOG.md..."

    # 拆出尾注（从第一个 "Under GPL v3 License" 开始）
    $footerIndex = -1
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^Under GPL v3 License') {
            $footerIndex = $i
            break
        }
    }

    if ($footerIndex -ge 0) {
        $head = $lines[0..($footerIndex - 1)]
        $footer = $lines[$footerIndex..($lines.Count - 1)]

        $newHead = @()
        $newHead += $head
        $newHead += "---v $newVersion---"
        $newHead += $logLine

        # 重新组装：head + 新日志 + 空行 + footer
        $final = @()
        $final += $newHead
        $final += ""
        $final += $footer

        Set-Content -Path "docs/VERSION_LOG.md" -Value $final -Encoding UTF8
    }
    else {
        # 没有找到尾注标记，就直接紧贴末尾追加
        $lines += "---v $newVersion---"
        $lines += $logLine
        Set-Content -Path "docs/VERSION_LOG.md" -Value $lines -Encoding UTF8
    }
}
else {
    Write-Warning "docs/VERSION_LOG.md 不存在，跳过"
}

# [4/4] 更新 packaging/rpm-build/sfd-tool.spec 中的 Version 字段，并可选更新 %changelog
if (Test-Path "packaging/rpm-build/sfd-tool.spec") {
    Write-Host "[4/4] 更新 packaging/rpm-build/sfd-tool.spec 版本号..."
    $specPath = "packaging/rpm-build/sfd-tool.spec"
    $specLines = Get-Content $specPath

    # 更新 Version 行
    for ($i = 0; $i -lt $specLines.Count; $i++) {
        if ($specLines[$i] -match '^(Version:\s*)([0-9.]+)') {
            $prefix = $Matches[1]
            $specLines[$i] = "$prefix$newVersion"
            break
        }
    }

    # 更新 %changelog：在现有 %changelog 顶部插入新条目
    $rpmDate = Get-Date -Format "ddd MMM dd yyyy"
    $rpmVer  = "$newVersion-1-ltv"
    $output = New-Object System.Collections.Generic.List[string]
    $inserted = $false
    for ($i = 0; $i -lt $specLines.Count; $i++) {
        $line = $specLines[$i]
        if (-not $inserted -and $line -match '^\%changelog') {
            $output.Add('%changelog')
            $output.Add("* $rpmDate RyanCrepa <Ryan110413@outlook.com> - $rpmVer")
            $output.Add("- $logLine")
            $output.Add("")
            $inserted = $true
            continue  # 跳过原始 %changelog 行，其余按原样追加
        }
        $output.Add($line)
    }
    if (-not $inserted) {
        # 如果 spec 里没有 %changelog，就简单把原内容加上新的 %changelog 段
        $output.Add('%changelog')
        $output.Add("* $rpmDate RyanCrepa <Ryan110413@outlook.com> - $rpmVer")
        $output.Add("- $logLine")
        $output.Add("")
    }

    Set-Content -Path $specPath -Value $output -Encoding UTF8
}
else {
    Write-Warning "packaging/rpm-build/sfd-tool.spec 不存在，跳过"
}

# 附加：更新 Debian changelog（packaging/debian/changelog）
if (Test-Path "packaging/debian/changelog") {
    Write-Host "[附加] 更新 packaging/debian/changelog..."
    $debVersion = "$newVersion-1-ltv"
    $debDate = Get-Date -Format "ddd, dd MMM yyyy HH:mm:ss K"
    $changelogPath = "packaging/debian/changelog"
    $existing = Get-Content $changelogPath

    $newEntry = @()
    $newEntry += "sfd-tool ($debVersion) unstable; urgency=medium"
    $newEntry += ""
    $newEntry += "  * $logLine"
    $newEntry += ""
    $newEntry += " -- RyanCrepa <Ryan110413@outlook.com>  $debDate"
    $newEntry += ""

    $finalChangelog = @()
    $finalChangelog += $newEntry
    $finalChangelog += $existing

    Set-Content -Path $changelogPath -Value $finalChangelog -Encoding UTF8
}
else {
    Write-Warning "packaging/debian/changelog 不存在，跳过"
}

# 附加：更新 Windows 资源文件中的版本
if (Test-Path "app.rc") {
    Write-Host "[附加] 更新 app.rc 里的版本号..."
    $rcText = Get-Content "app.rc" -Raw

    $versionComma = ($newVersion -replace '\.', ',')

    # 数字版本: FILEVERSION / PRODUCTVERSION
    $rcText = $rcText -replace 'FILEVERSION\s+[0-9,]+', "FILEVERSION $versionComma"
    $rcText = $rcText -replace 'PRODUCTVERSION\s+[0-9,]+', "PRODUCTVERSION $versionComma"

    # 字符串版本: "FileVersion" / "ProductVersion"
    $rcText = $rcText -replace 'VALUE "FileVersion",\s*"[0-9.]+"', { param($m) 'VALUE "FileVersion", "' + $newVersion + '"' }
    $rcText = $rcText -replace 'VALUE "ProductVersion",\s*"[0-9.]+"', { param($m) 'VALUE "ProductVersion", "' + $newVersion + '"' }

    Set-Content -Path "app.rc" -Value $rcText -Encoding UTF8
}
else {
    Write-Warning "app.rc 不存在，跳过"
}

Write-Host ""
Write-Host "Git 变更预览：" -ForegroundColor Cyan
& git status --short VERSION.txt docs/VERSION_LOG.md packaging/rpm-build/sfd-tool.spec app.rc

Write-Host ""
Write-Host "正在提交版本变更..." -ForegroundColor Cyan
git add VERSION.txt
if (Test-Path "docs/VERSION_LOG.md") {
    git add docs/VERSION_LOG.md
}
if (Test-Path "packaging/rpm-build/sfd-tool.spec") {
    git add packaging/rpm-build/sfd-tool.spec
}
if (Test-Path "app.rc") {
    git add app.rc
}

git commit -m ("Version: " + $newVersion)

Write-Host "完成：已更新版本号为 $newVersion 并提交到 git。" -ForegroundColor Green
Write-Host "记得重新跑 CMake 配置和编译以生成新的 version.h。" -ForegroundColor Yellow
