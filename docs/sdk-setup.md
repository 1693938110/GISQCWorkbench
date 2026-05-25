# Windows / WSL 开发 SDK 准备

本项目的真实功能开发需要两类 SDK：

1. **Qt 6 Widgets SDK**：构建桌面客户端界面。
2. **GDAL / PROJ SDK**：读取 GIS 数据源真实元数据、坐标系、图层、字段和要素信息。

## WSL / Ubuntu 一键安装

项目内已提供脚本：

```bash
cd /mnt/d/jiedan/136/gis-qc-workbench
sudo bash scripts/install_ubuntu_wsl_sdks.sh
```

安装内容包括：

```text
build-essential
cmake
ninja-build
pkg-config
qt6-base-dev
qt6-tools-dev
qt6-tools-dev-tools
libgdal-dev
gdal-bin
proj-bin
```

验证命令：

```bash
cmake --version
qmake6 --version
gdal-config --version
pkg-config --modversion Qt6Widgets
```

## 当前环境注意

本机这次执行 `apt-get install` 时，Ubuntu 官方源 `archive.ubuntu.com` / `security.ubuntu.com` 连接超时，SDK 未能完整下载。脚本已保留在项目里，网络或镜像源恢复后可直接重跑。

## Windows 交付建议

最终给客户交付 Win10+ 安装包时，建议使用以下方式之一：

- **OSGeo4W**：安装 GDAL/PROJ/GEOS 等 GIS 运行库。
- **Qt Online Installer / Qt Maintenance Tool**：安装 MSVC 或 MinGW 对应的 Qt 6 Widgets。
- 使用 CMake preset 指向 Windows 端 Qt 和 GDAL 安装目录。

后续接 GDAL C++ API 时，CMake 里应使用：

```cmake
find_package(GDAL REQUIRED)
target_link_libraries(gisqc_core PRIVATE GDAL::GDAL)
```

在 SDK 不可用的环境下，当前代码已先实现 Shapefile 的轻量真实元数据读取：

- 从 `.dbf` 文件头读取记录数。
- 从 `.prj` 文件识别 CGCS2000 / WGS84。
- 检查 `.shp/.shx/.dbf/.prj` 配套文件完整性。

这部分不依赖 GDAL，可作为 GDAL 接入前的真实可用功能。
