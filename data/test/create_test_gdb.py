"""
Create a test FileGDB with various geometry issues for quality check testing.
Requires: osgeo (GDAL Python bindings)
Usage: python create_test_gdb.py
"""
import os
import shutil
from osgeo import ogr, osr

OUTPUT_DIR = os.path.dirname(os.path.abspath(__file__))
GDB_PATH = os.path.join(OUTPUT_DIR, "test_data.gdb")
ROOT_DIR = os.path.join(OUTPUT_DIR, "test_root")

# Clean up
if os.path.exists(GDB_PATH):
    shutil.rmtree(GDB_PATH)
if os.path.exists(ROOT_DIR):
    shutil.rmtree(ROOT_DIR)

# Create root directory structure for A-category checks
os.makedirs(os.path.join(ROOT_DIR, "空间数据"), exist_ok=True)
os.makedirs(os.path.join(ROOT_DIR, "文档资料"), exist_ok=True)
os.makedirs(os.path.join(ROOT_DIR, "元数据"), exist_ok=True)
os.makedirs(os.path.join(ROOT_DIR, "空目录测试"), exist_ok=True)  # empty dir -> A010301
# Missing required file -> A010104
with open(os.path.join(ROOT_DIR, "readme.txt"), "w") as f:
    f.write("test")
# Bad filename -> A010302
with open(os.path.join(ROOT_DIR, "空间数据", "不规范 文件名.txt"), "w") as f:
    f.write("bad name")

# Create CGCS2000 SRS
srs_4490 = osr.SpatialReference()
srs_4490.ImportFromEPSG(4490)

# Wrong CRS (WGS84 instead of CGCS2000) -> C010201
srs_4326 = osr.SpatialReference()
srs_4326.ImportFromEPSG(4326)

driver = ogr.GetDriverByName("OpenFileGDB")
if driver is None:
    driver = ogr.GetDriverByName("FileGDB")
if driver is None:
    # Fallback: use GPKG
    GDB_PATH = os.path.join(ROOT_DIR, "空间数据", "test_data.gpkg")
    driver = ogr.GetDriverByName("GPKG")

ds = driver.CreateDataSource(GDB_PATH)

# === Layer: 点图层 (Points) ===
pt_layer = ds.CreateLayer("点图层", srs_4326, ogr.wkbPoint)
pt_layer.CreateField(ogr.FieldDefn("NAME", ogr.OFTString))
pt_layer.CreateField(ogr.FieldDefn("TYPE", ogr.OFTInteger))

# Normal point
feat = ogr.Feature(pt_layer.GetLayerDefn())
feat.SetField("NAME", "正常点A")
feat.SetField("TYPE", 1)
feat.SetGeometry(ogr.CreateGeometryFromWkt("POINT (116.4 39.9)"))
pt_layer.CreateFeature(feat)

# Duplicate point -> C030101
feat = ogr.Feature(pt_layer.GetLayerDefn())
feat.SetField("NAME", "重复点B")
feat.SetField("TYPE", 1)
feat.SetGeometry(ogr.CreateGeometryFromWkt("POINT (116.4 39.9)"))
pt_layer.CreateFeature(feat)

# Multi-part point -> C030102
feat = ogr.Feature(pt_layer.GetLayerDefn())
feat.SetField("NAME", "多部件点")
feat.SetField("TYPE", 2)
feat.SetGeometry(ogr.CreateGeometryFromWkt("MULTIPOINT ((116.5 39.8), (116.6 39.7))"))
pt_layer.CreateFeature(feat)

# === Layer: 线图层 (Lines) ===
ln_layer = ds.CreateLayer("线图层", srs_4490, ogr.wkbLineString)
ln_layer.CreateField(ogr.FieldDefn("NAME", ogr.OFTString))

# Short line -> C020201 (length < tolerance)
feat = ogr.Feature(ln_layer.GetLayerDefn())
feat.SetField("NAME", "短线")
feat.SetGeometry(ogr.CreateGeometryFromWkt("LINESTRING (116.0 39.0, 116.00001 39.00001)"))
ln_layer.CreateFeature(feat)

# Self-intersecting line -> C030207
feat = ogr.Feature(ln_layer.GetLayerDefn())
feat.SetField("NAME", "自相交线")
feat.SetGeometry(ogr.CreateGeometryFromWkt("LINESTRING (116.1 39.1, 116.3 39.3, 116.1 39.3, 116.3 39.1)"))
ln_layer.CreateFeature(feat)

# Self-overlapping line -> C030206
feat = ogr.Feature(ln_layer.GetLayerDefn())
feat.SetField("NAME", "自重叠线")
feat.SetGeometry(ogr.CreateGeometryFromWkt("LINESTRING (116.5 39.5, 116.6 39.5, 116.5 39.5, 116.6 39.5)"))
ln_layer.CreateFeature(feat)

# Multipart line -> C030208
feat = ogr.Feature(ln_layer.GetLayerDefn())
feat.SetField("NAME", "多部件线")
feat.SetGeometry(ogr.CreateGeometryFromWkt("MULTILINESTRING ((116.7 39.0, 116.8 39.1), (117.0 39.0, 117.1 39.1))"))
ln_layer.CreateFeature(feat)

# Normal line (for overlap test pair) -> C030201
feat = ogr.Feature(ln_layer.GetLayerDefn())
feat.SetField("NAME", "重叠线A")
feat.SetGeometry(ogr.CreateGeometryFromWkt("LINESTRING (117.2 39.0, 117.4 39.2)"))
ln_layer.CreateFeature(feat)
feat = ogr.Feature(ln_layer.GetLayerDefn())
feat.SetField("NAME", "重叠线B")
feat.SetGeometry(ogr.CreateGeometryFromWkt("LINESTRING (117.2 39.0, 117.4 39.2)"))
ln_layer.CreateFeature(feat)

# === Layer: 面图层 (Polygons) ===
pg_layer = ds.CreateLayer("面图层", srs_4490, ogr.wkbPolygon)
pg_layer.CreateField(ogr.FieldDefn("NAME", ogr.OFTString))
pg_layer.CreateField(ogr.FieldDefn("AREA_TYPE", ogr.OFTString))

# Normal polygon
feat = ogr.Feature(pg_layer.GetLayerDefn())
feat.SetField("NAME", "正常面")
feat.SetField("AREA_TYPE", "建设用地")
feat.SetGeometry(ogr.CreateGeometryFromWkt("POLYGON ((116.0 39.0, 116.5 39.0, 116.5 39.5, 116.0 39.5, 116.0 39.0))"))
pg_layer.CreateFeature(feat)

# Sharp angle polygon -> C020302 (very acute angle)
feat = ogr.Feature(pg_layer.GetLayerDefn())
feat.SetField("NAME", "尖锐角面")
feat.SetField("AREA_TYPE", "建设用地")
feat.SetGeometry(ogr.CreateGeometryFromWkt("POLYGON ((117.0 39.0, 117.5 39.0, 117.0001 39.5, 117.0 39.0))"))
pg_layer.CreateFeature(feat)

# Tiny polygon -> C020303
feat = ogr.Feature(pg_layer.GetLayerDefn())
feat.SetField("NAME", "微小面")
feat.SetField("AREA_TYPE", "其他")
feat.SetGeometry(ogr.CreateGeometryFromWkt("POLYGON ((118.0 39.0, 118.00001 39.0, 118.00001 39.00001, 118.0 39.00001, 118.0 39.0))"))
pg_layer.CreateFeature(feat)

# Short edge polygon -> C020301
feat = ogr.Feature(pg_layer.GetLayerDefn())
feat.SetField("NAME", "超短边面")
feat.SetField("AREA_TYPE", "其他")
feat.SetGeometry(ogr.CreateGeometryFromWkt("POLYGON ((118.5 39.0, 118.50001 39.0, 118.6 39.1, 118.5 39.1, 118.5 39.0))"))
pg_layer.CreateFeature(feat)

# Self-intersecting polygon -> C030303
feat = ogr.Feature(pg_layer.GetLayerDefn())
feat.SetField("NAME", "自相交面")
feat.SetField("AREA_TYPE", "建设用地")
feat.SetGeometry(ogr.CreateGeometryFromWkt("POLYGON ((119.0 39.0, 119.5 39.5, 119.0 39.5, 119.5 39.0, 119.0 39.0))"))
pg_layer.CreateFeature(feat)

# Polygon with hole -> C030305
feat = ogr.Feature(pg_layer.GetLayerDefn())
feat.SetField("NAME", "有孔洞面")
feat.SetField("AREA_TYPE", "建设用地")
feat.SetGeometry(ogr.CreateGeometryFromWkt("POLYGON ((120.0 39.0, 120.5 39.0, 120.5 39.5, 120.0 39.5, 120.0 39.0), (120.1 39.1, 120.2 39.1, 120.2 39.2, 120.1 39.2, 120.1 39.1))"))
pg_layer.CreateFeature(feat)

# Multipart polygon -> C030306
feat = ogr.Feature(pg_layer.GetLayerDefn())
feat.SetField("NAME", "多部件面")
feat.SetField("AREA_TYPE", "其他")
feat.SetGeometry(ogr.CreateGeometryFromWkt("MULTIPOLYGON (((121.0 39.0, 121.1 39.0, 121.1 39.1, 121.0 39.1, 121.0 39.0)), ((121.5 39.0, 121.6 39.0, 121.6 39.1, 121.5 39.1, 121.5 39.0)))"))
pg_layer.CreateFeature(feat)

# Overlapping polygons -> C030302
feat = ogr.Feature(pg_layer.GetLayerDefn())
feat.SetField("NAME", "重叠面A")
feat.SetField("AREA_TYPE", "建设用地")
feat.SetGeometry(ogr.CreateGeometryFromWkt("POLYGON ((122.0 39.0, 122.3 39.0, 122.3 39.3, 122.0 39.3, 122.0 39.0))"))
pg_layer.CreateFeature(feat)
feat = ogr.Feature(pg_layer.GetLayerDefn())
feat.SetField("NAME", "重叠面B")
feat.SetField("AREA_TYPE", "建设用地")
feat.SetGeometry(ogr.CreateGeometryFromWkt("POLYGON ((122.1 39.1, 122.4 39.1, 122.4 39.4, 122.1 39.4, 122.1 39.1))"))
pg_layer.CreateFeature(feat)

# === Layer: 参照面图层 (for inter-layer topology tests) ===
ref_layer = ds.CreateLayer("参照面图层", srs_4490, ogr.wkbPolygon)
ref_layer.CreateField(ogr.FieldDefn("NAME", ogr.OFTString))

# Reference area covering part of the point/line areas
feat = ogr.Feature(ref_layer.GetLayerDefn())
feat.SetField("NAME", "参照区域")
feat.SetGeometry(ogr.CreateGeometryFromWkt("POLYGON ((116.0 38.5, 117.0 38.5, 117.0 40.0, 116.0 40.0, 116.0 38.5))"))
ref_layer.CreateFeature(feat)

# === Layer: 参照点图层 (for inter-layer point topology) ===
ref_pt_layer = ds.CreateLayer("参照点图层", srs_4490, ogr.wkbPoint)
ref_pt_layer.CreateField(ogr.FieldDefn("NAME", ogr.OFTString))

feat = ogr.Feature(ref_pt_layer.GetLayerDefn())
feat.SetField("NAME", "参照点1")
feat.SetGeometry(ogr.CreateGeometryFromWkt("POINT (116.4 39.9)"))
ref_pt_layer.CreateFeature(feat)

ds = None

# Copy/symlink GDB into the test root
if GDB_PATH.endswith(".gdb"):
    dest = os.path.join(ROOT_DIR, "空间数据", "test_data.gdb")
    if not os.path.exists(dest):
        shutil.copytree(GDB_PATH, dest)

print(f"Test data created at: {ROOT_DIR}")
print(f"GDB path: {GDB_PATH}")
print(f"Layers: 点图层, 线图层, 面图层, 参照面图层, 参照点图层")
