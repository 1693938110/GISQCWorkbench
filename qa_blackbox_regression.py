import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(r"D:/jiedan/136/gis-qc-workbench")
RELEASE = ROOT / "build-windows-release" / "Release"
CLI = RELEASE / "GISQCWorkbenchCLI.exe"
GEN = RELEASE / "qa_generate_synthetic_data.exe"
DATA = ROOT / "qa_synthetic_data"
OUT = ROOT / "qa_regression_outputs"


def run(cmd, cwd=RELEASE, env=None):
    cp = subprocess.run([str(x) for x in cmd], cwd=str(cwd), capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120, env=env)
    return cp


def parse_issues(stdout):
    m = re.search(r"Issues:\s*(\d+)", stdout)
    return int(m.group(1)) if m else None


def assert_true(cond, msg):
    if not cond:
        raise AssertionError(msg)


def main():
    assert_true(CLI.exists(), f"missing CLI: {CLI}")
    assert_true(GEN.exists(), f"missing fixture generator: {GEN}")
    OUT.mkdir(parents=True, exist_ok=True)

    # Generate fixtures exactly like a tester would prepare data, without setting external env.
    gen = run([GEN])
    assert_true(gen.returncode == 0, f"fixture generator failed: {gen.stdout}\n{gen.stderr}")

    # 1) Chinese output filename must actually be created and non-empty.
    chinese_out = OUT / "中文输出结果.csv"
    if chinese_out.exists():
        chinese_out.unlink()
    cp = run([CLI, DATA / "02_缺失_命名_配套文件问题包", chinese_out])
    assert_true(cp.returncode == 2, f"expected return 2 for data with issues, got {cp.returncode}\n{cp.stdout}\n{cp.stderr}")
    assert_true(chinese_out.exists(), f"Chinese output CSV was not created. stdout:\n{cp.stdout}")
    assert_true(chinese_out.stat().st_size > 100, f"Chinese output CSV too small: {chinese_out.stat().st_size}")

    # 2) Nonexistent input directory must be an execution error, not success.
    missing_out = OUT / "missing.csv"
    if missing_out.exists():
        missing_out.unlink()
    cp = run([CLI, DATA / "不存在目录", missing_out])
    assert_true(cp.returncode != 0, f"nonexistent input should fail, got return 0\n{cp.stdout}\n{cp.stderr}")
    combined = cp.stdout + cp.stderr
    assert_true("不存在" in combined or "not" in combined.lower() or "No such" in combined, f"missing-dir error message not clear:\n{combined}")

    # 3) Direct invocation from deployed release dir must not need external GDAL/PROJ env.
    clean_out = OUT / "clean.csv"
    if clean_out.exists():
        clean_out.unlink()
    env = os.environ.copy()
    for key in ["GDAL_DATA", "PROJ_DATA", "PROJ_LIB"]:
        env.pop(key, None)
    cp = run([CLI, DATA / "01_正常基础包", clean_out], env=env)
    assert_true(cp.returncode == 0, f"clean package should return 0 without external env, got {cp.returncode}\n{cp.stdout}\n{cp.stderr}")
    assert_true(parse_issues(cp.stdout) == 0, f"clean package should have 0 issues\n{cp.stdout}\n{cp.stderr}")
    assert_true(clean_out.exists(), "clean output CSV was not created")

    print("black-box CLI regressions passed")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"REGRESSION FAILED: {exc}", file=sys.stderr)
        sys.exit(1)
