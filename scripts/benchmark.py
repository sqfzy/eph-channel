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
from scipy import stats

# --- 全局配置 ---
BASE_DIR = Path(__file__).resolve().parent.parent
BENCHMARK_DIR = BASE_DIR / "benchmark"
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

def add_kde_trace(fig, data, row, col, color='purple', name='Density'):
    """计算并添加 KDE 曲线"""
    try:
        kde = stats.gaussian_kde(data)
        x_range = np.linspace(min(data), max(data), 500)
        y_kde = kde(x_range)
        fig.add_trace(
            go.Scatter(x=x_range, y=y_kde, mode='lines', name=name, line=dict(color=color, width=2)),
            row=row, col=col
        )
    except Exception as e:
        print(f"   [!] KDE 计算略过: {e}")

# --- 报告生成逻辑 ---

def generate_report(name: str):
    """动态定位最新的 CSV 并生成 Plotly HTML 报告"""
    # 适配文件名格式: bench_{name}*.csv
    csv_path = get_latest_file(f"bench_{name}*.csv")
    
    if not csv_path:
        # 兜底尝试查找简单命名的文件
        csv_path = OUTPUT_DIR / f"{name}.csv"
        if not csv_path.exists():
            print(f"   [跳过] 未找到对应的 CSV 数据文件")
            return

    output_html = OUTPUT_DIR / f"{name}_latency_report.html"
    print(f"   [报告] 正在分析最新数据: {csv_path.name}")
    
    # 1. 数据加载
    df = pl.read_csv(csv_path)
    # 2. P99 过滤离群点
    upper_bound = df["latency_ns"].quantile(0.99)
    df_clean = df.filter(pl.col("latency_ns") <= upper_bound)

    # 3. 创建子图布局
    fig = make_subplots(
        rows=3, cols=1,
        subplot_titles=(
            f"Time Series (Full Range)", 
            "Latency Distribution (Log Scale)", 
            "P99 Body Distribution (Linear + KDE)"
        ),
        vertical_spacing=0.1
    )

    # 图表 1: Time Series
    fig.add_trace(
        go.Scatter(x=df["seq"], y=df["latency_ns"], mode='lines', name='Latency', line=dict(width=1, color='#1f77b4')),
        row=1, col=1
    )

    # 图表 2: Log Distribution
    fig.add_trace(
        go.Histogram(x=df["latency_ns"], nbinsx=1000, name='Full Log', marker_color='#636EFA'),
        row=2, col=1
    )
    fig.update_yaxes(type="log", row=2, col=1)

    # 图表 3: Linear Body + KDE
    fig.add_trace(
        go.Histogram(
            x=df_clean["latency_ns"], nbinsx=500, 
            name='Body', marker_color='#00CC96', opacity=0.6,
            histnorm='probability density'
        ),
        row=3, col=1
    )
    add_kde_trace(fig, df_clean["latency_ns"].to_numpy(), row=3, col=1)
    
    mean_val = df_clean["latency_ns"].mean()
    fig.add_vline(x=mean_val, line_dash="dash", line_color="red", row=3, col=1)

    # 布局微调
    fig.update_layout(
        # height=1000, 
        title_text=f"Benchmark Comprehensive Report: {name}<br><sup>Source: {csv_path.name}</sup>",
        showlegend=False, 
        template="plotly_white"
    )
    
    fig.write_html(str(output_html))
    print(f"   [报告] HTML 已保存: {output_html.name}")

# --- 任务执行逻辑 ---

def run_single_bench(name: str):
    target_bin = f"benchmark_{name}"
    exec_path = BUILD_DIR / target_bin
    needs_roudi = ("iox" in name) # 更加通用的判断

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
    generate_report(name)

def main():
    # 获取目录下的全部可选项
    cpp_files = (BENCHMARK_DIR / "examples").glob("*.cpp")
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
        json_file = get_latest_file(f"bench_{t}*.json")
        summary.append({
            "Target": t,
            "Status": "✅" if json_file else "❌",
            "Latest_Data": json_file.name if json_file else "N/A"
        })
    print(pl.DataFrame(summary))
    print("="*50)

if __name__ == "__main__":
    main()
