# Windows 开发环境准备

本工程目标运行环境是 Win10+。建议在 Windows 上配置以下工具链：

## 1. 必装工具

- Visual Studio 2022，选择“使用 C++ 的桌面开发”
- CMake 3.25+
- Ninja，可选
- Qt 6.5+ / 6.6+，安装 MSVC 2022 64-bit 套件
- Git

## 2. GIS 依赖

建议优先使用 OSGeo4W 安装：

- GDAL / OGR
- GEOS
- PROJ
- SQLite

后续 CMake 会增加 `FindGDAL`、`FindGEOS`、`FindSQLite3` 检测。

## 3. 构建命令

```powershell
cd D:\jiedan\136\gis-qc-workbench
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="C:\Qt\6.6.3\msvc2019_64"
cmake --build build
.\build\GISQCWorkbench.exe
```

## 4. 当前 WSL 状态

当前 WSL 环境 apt 源连接超时，无法安装 Qt/GDAL/CMake，因此只验证了无 Qt 依赖的 core 模块：

```bash
make test-core
```

通过后再在 Windows Qt 环境里验证完整桌面窗口。
