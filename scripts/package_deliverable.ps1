param(
    [string]$SourceDir = "dist\GISQCWorkbench-latest",
    [string]$PackageDir = "dist\packages"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $SourceDir)) {
    throw "Source directory not found: $SourceDir. Run scripts\build_windows_release.ps1 first."
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$name = "GISQCWorkbench-deliverable-$timestamp"
$stage = Join-Path $PackageDir $name
$zip = Join-Path $PackageDir "$name.zip"

New-Item -ItemType Directory -Force -Path $PackageDir | Out-Null
if (Test-Path $stage) {
    Remove-Item $stage -Recurse -Force
}

Copy-Item $SourceDir -Destination $stage -Recurse -Force

$manifestLines = @(
  "GIS 数据质量检查工作台交付包",
  "生成时间：$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')",
  "版本定位：对外试用版 / 阶段性交付版",
  "",
  "启动入口：",
  "  GISQCWorkbench.exe",
  "",
  "辅助入口：",
  "  GISQCWorkbenchCLI.exe",
  "  GISQCWorkbenchWinPreview.exe",
  "",
  "随包文档：",
  "  README_本轮构建说明.txt",
  "  交付说明.txt",
  "  docs\\快速开始_试用版.md",
  "  docs\\支持规则清单.md",
  "  docs\\交付验收清单.md",
  "",
  "配置说明：",
  "  data\\templates\\default_rules.json 为内置默认规则。",
  "  data\\templates\\user_rules.json 为软件内保存后的用户配置，优先级高于默认规则。",
  "  运行配置已迁移到软件内部，不再依赖 Excel 配置表。",
  "  data\\history\\task_history.tsv 为软件自动生成的任务历史记录。",
  "",
  "运行库：",
  "  当前包已包含 Qt、MSVC、GDAL、PROJ 等运行依赖。",
  "",
  "报告能力：",
  "  支持 CSV、HTML、Excel 兼容、Word 兼容报告导出。",
  "  本轮报告模板已补充检查范围、规则清单说明、统计摘要、按规则统计、按图层统计、问题明细、检查结论和处理建议。",
  "",
  "验收建议：",
  "  1. 双击 GISQCWorkbench.exe 能打开主界面。",
  "  2. 数据导入页能选择成果目录并扫描数据源。",
  "  3. 规则配置页能保存内部配置。",
  "  4. 执行质检页能运行任务。",
  "  5. 首页最近任务能显示执行记录。",
  "  6. 结果中心能查看统计、问题清单并导出 CSV/HTML/Excel/Word 兼容报告。",
  "  7. docs\\支持规则清单.md 能说明当前版本支持规则范围和启用状态。"
)
$manifest = $manifestLines -join [Environment]::NewLine

Set-Content -Path (Join-Path $stage "交付说明.txt") -Value $manifest -Encoding UTF8

if (Test-Path $zip) {
    Remove-Item $zip -Force
}
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip -Force

Write-Host "Generated package directory: $stage"
Write-Host "Generated package zip: $zip"

