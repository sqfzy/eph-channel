import os
import sys
import subprocess
import json
import time
from pathlib import Path

# 数据处理与绘图
import polars as pl
import numpy as np
import plotly.graph_objects as go
from plotly.subplots import make_subplots

# --- 全局配置 ---
BASE_DIR = Path(__file__).resolve().parent.parent
BENCHMARK_DIR = BASE_DIR / "benchmarks"
OUTPUT_DIR = BASE_DIR / "outputs"
BUILD_DIR = BASE_DIR / "build" / "linux" / "x86_64" / "release"

# 确保输出目录存在
OUTPUT_DIR.mkdir(exist_ok=True)

# --- 核心辅助函数 ---

def get_latest_file(pattern: str) -> Path | None:
    """根据模式获取目录下最新的文件"""
    matches = list(OUTPUT_DIR.glob(pattern))
    if not matches:
        return None
    # 按修改时间排序，取最后一个
    return max(matches, key=lambda p: p.stat().st_mtime)

# --- 任务执行逻辑 ---

def run_single_bench(name: str):
    target_bin = f"bench_{name}"
    exec_path = BUILD_DIR / target_bin
    needs_roudi = ("iox" in name) 

    print(f"\n🚀 开始执行测试目标: {name}")

    # A. 编译 (xmake)
    print(f"   [1/3] 构建中...")
    if subprocess.run(["xmake", "build", target_bin], cwd=BASE_DIR, capture_output=True).returncode != 0:
        print(f"   ❌ 构建失败: {target_bin}")
        return

    # B. 运行 (考虑 RouDi 环境)
    roudi_proc = None
    try:
        if needs_roudi:
            print("   [2/3] 启动 iox-roudi 环境...")
            f_log = open(BASE_DIR / "roudi.log", "w")
            roudi_proc = subprocess.Popen(["sudo", "iox-roudi"], stdout=f_log, stderr=subprocess.STDOUT)
            time.sleep(1.5)

        print("   [3/3] 运行测试二进制程序...")
        subprocess.run(["sudo", str(exec_path)], check=True, cwd=BASE_DIR)
    except Exception as e:
        print(f"   ❌ 运行时出错: {e}")
    finally:
        if roudi_proc:
            print("   [清理] 正在关闭 iox-roudi...")
            subprocess.run(["sudo", "pkill", "-x", "iox-roudi"], check=False)
            roudi_proc.terminate()

    # C. 自动分析最新生成的 CSV
    print(f"   [报告] 启动分析脚本...")
    subprocess.run([sys.executable, "scripts/gen_report.py", name], cwd=BASE_DIR)

def main():
    # 获取目录下的全部可选项
    cpp_files = (BENCHMARK_DIR).glob("*.cpp")
    available_targets = [f.stem for f in cpp_files]

    print("📋 可用测试目标:")
    for t in available_targets:
        print(f"   - {t}")
    
    user_args = sys.argv[1:]
    run_list = user_args if user_args else available_targets

    # 验证输入
    for t in run_list:
        if t not in available_targets:
            print(f"❌ 找不到目标: {t}\n可选目标: {available_targets}")
            sys.exit(1)

    # 提权一次 sudo
    subprocess.run(["sudo", "-v"], check=True)

    for target in run_list:
        run_single_bench(target)

    # 汇总展示
    print("\n" + "="*50)
    print("📊 最终状态汇总")
    summary = []
    for t in run_list:
        json_file = get_latest_file(f"{t}*.json") # 同样更新这里的查找逻辑
        summary.append({
            "Target": t,
            "Status": "✅" if json_file else "❌",
            "Latest_Data": json_file.name if json_file else "N/A"
        })
    print(pl.DataFrame(summary))
    print("="*50)

if __name__ == "__main__":
    main()
