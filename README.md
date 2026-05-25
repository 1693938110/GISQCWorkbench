# GIS 数据质检工作台（C++ / Qt）

这是为 `D:\jiedan\136` GIS 数据通用质检项目新建的桌面客户端工程，目标是替代原来的 FME + Excel 配置表流程。

## 当前定位

- Windows 10+ 桌面端应用
- C++17/20 + Qt 6 Widgets + QSS
- 后续接入 GDAL/OGR、GEOS、PROJ、SQLite
- 第一版先完成：数据导入、规则配置、执行质检、结果中心、报告导出

## 目录结构

```text
src/core      无 Qt 依赖的核心模型/扫描/规则/结果逻辑
src/app       应用入口
src/ui        Qt Widgets 页面和窗口
src/resources QSS 样式
Tests         核心逻辑单元测试
data/templates 默认规则模板
scripts       构建/打包脚本
```

## 当前环境说明

当前 WSL 环境只有 `g++` 可用，`apt` 访问 Ubuntu 源超时，暂时无法安装 Qt/GDAL/CMake。  
因此本次先提交：

1. 可用 `g++` 编译验证的 core 模块；
2. Qt Widgets 源码骨架；
3. 原型图对应页面布局代码；
4. 默认规则模板；
5. 后续 Windows 开发环境构建说明。

## 核心测试

```bash
make test-core
```

## Qt 完整构建，待 Windows/Qt 环境启用

```bash
cmake -S . -B build -G Ninja
cmake --build build
```
