# GIS 数据质检工作台 C++/Qt V1 Implementation Plan

> **For Hermes:** Use subagent-driven-development skill to implement this plan task-by-task.

**Goal:** 按已确认原型图开发 Win10+ C++/Qt GIS 数据质检桌面客户端 V1 骨架。

**Architecture:** 采用 Core + App/UI 分层。`src/core` 保持无 Qt 依赖，便于单元测试；`src/app` / `src/ui` 使用 Qt Widgets + QSS 还原原型界面；后续 `src/gis` 接入 GDAL/OGR、GEOS、PROJ。

**Tech Stack:** C++17/20, Qt 6 Widgets, CMake, GDAL/OGR, GEOS, PROJ, SQLite, libxlsxwriter/QtXlsx, Inno Setup.

---

## 阶段 0：当前已开始的技术验证

### Task 1: 创建工程骨架

**Objective:** 建立可持续开发的目录结构、构建脚本、README、阶段计划。

**Files:**
- Create: `CMakeLists.txt`
- Create: `Makefile`
- Create: `README.md`
- Create: `docs/plans/2026-05-21-v1-development-plan.md`

**Verification:** `g++` 可编译核心测试；Qt/GDAL 依赖未安装时不阻断核心开发。

### Task 2: TDD 实现数据集扫描模型

**Objective:** 在无 GDAL 环境下先实现可测试的文件/目录扫描基础，后续替换为 GDAL 深度读取。

**Files:**
- Create: `src/core/DatasetScanner.h`
- Create: `src/core/DatasetScanner.cpp`
- Create: `tests/test_dataset_scanner.cpp`

**Behavior:**
- 识别 `.gdb` 目录为 FileGDB。
- 识别 `.shp` 文件为 Shapefile 图层。
- 识别 `.gpkg` 文件为 GeoPackage。
- 返回数据源类型、图层名、状态、路径。

### Task 3: 搭建 Qt 主窗口源码骨架

**Objective:** 用 Qt Widgets 还原原型图布局：左侧导航、中间页面、右侧面板、底部日志。

**Files:**
- Create: `src/app/main.cpp`
- Create: `src/ui/MainWindow.h/.cpp`
- Create: `src/ui/DashboardPage.h/.cpp`
- Create: `src/ui/RuleConfigPage.h/.cpp`
- Create: `src/ui/ResultsPage.h/.cpp`
- Create: `src/resources/app.qss`

### Task 4: 默认规则模板

**Objective:** 建立第一版规则模板 JSON，为后续 SQLite 迁移做准备。

**Files:**
- Create: `data/templates/default_rules.json`

### Task 5: 下一步 GDAL/SQLite 集成

**Objective:** 在 Windows/可联网开发环境安装 Qt/GDAL 后启用完整 CMake 构建。

**Verification:**
- `cmake -S . -B build -G Ninja`
- `cmake --build build`
- 运行 `GISQCWorkbench` 打开桌面窗口。
