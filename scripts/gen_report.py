import argparse
import sys
from pathlib import Path

import polars as pl
import plotly.graph_objects as go
from plotly.subplots import make_subplots

# --- 路径配置 ---
BASE_DIR = Path(__file__).resolve().parent.parent
OUTPUT_DIR = BASE_DIR / "outputs"

def get_latest_file(pattern: str) -> Path | None:
    """根据模式获取目录下最新的文件"""
    matches = list(OUTPUT_DIR.glob(pattern))
    if not matches:
        return None
    return max(matches, key=lambda p: p.stat().st_mtime)

def generate_report(name: str):
    """动态定位最新的直方图 CSV 并生成 Plotly HTML 报告"""
    csv_path = get_latest_file(f"{name}*.csv")
    
    if not csv_path:
        print(f"   [错误] 未能在 {OUTPUT_DIR} 找到匹配 '{name}*.csv' 的数据文件")
        return

    # 打印必要信息
    print(f"   [数据源] {csv_path.resolve()}")
    
    output_html = OUTPUT_DIR / f"{csv_path.name}_report.html"
    
    try:
        # 1. 加载并计算统计量
        df = pl.read_csv(csv_path).sort("value_ns")
        total_count = df["count"].sum()
        if total_count == 0:
            print("   [跳过] 数据集样本数为 0")
            return

        # 计算全数据均值
        full_mean = (df["value_ns"] * df["count"]).sum() / total_count
        
        # 计算全数据 CDF
        df = df.with_columns(
            (pl.col("count").cum_sum() / total_count).alias("cdf")
        )
        
        # 计算关键百分位数
        full_p50 = df.filter(pl.col("cdf") >= 0.5).head(1)["value_ns"].item()
        p99_val = df.filter(pl.col("cdf") >= 0.99).head(1)["value_ns"].item()
        
        # 打印分析汇总
        print(f"   [统计] 样本总数: {total_count}")
        print(f"   [统计] 全局均值: {full_mean:.2f} ns")
        print(f"   [统计] 全局 P50 : {full_p50:.2f} ns")
        print(f"   [统计] 全局 P99 : {p99_val:.2f} ns (绘图截断点)")

        # 过滤用于绘图的数据 (仅限 P99)
        df_plot = df.filter(pl.col("value_ns") <= p99_val)

        # 2. 创建 2 行 1 列子图
        fig = make_subplots(
            rows=2, cols=1,
            subplot_titles=(
                "1. Cumulative Distribution (CDF) - Restricted to P99 Range", 
                "2. Latency Distribution (Linear) - Restricted to P99 Range"
            ),
            vertical_spacing=0.12
        )

        # --- 图表 1: CDF ---
        fig.add_trace(
            go.Scatter(
                x=df_plot["value_ns"], y=df_plot["cdf"], 
                mode='lines', name='CDF', 
                line=dict(width=3, color='#1f77b4')
            ),
            row=1, col=1
        )

        # --- 图表 2: 直方图 ---
        fig.add_trace(
            go.Bar(
                x=df_plot["value_ns"], y=df_plot["count"], 
                name='Count', marker_color='#00CC96'
            ),
            row=2, col=1
        )
        
        # --- 添加辅助线 (Mean 和 P50) ---
        for r in [1, 2]:
            # 全局 Mean 线
            fig.add_vline(
                x=full_mean, line_dash="dash", line_color="red", 
                annotation_text=f"Mean: {full_mean:.1f}ns", 
                annotation_position="top right",
                row=r, col=1
            )
            # 全局 P50 线
            fig.add_vline(
                x=full_p50, line_dash="dot", line_color="orange", 
                annotation_text=f"P50: {full_p50:.1f}ns", 
                annotation_position="top right",
                row=r, col=1
            )

        # 3. 布局设置
        fig.update_layout(
            height=850, 
            title_text=f"Benchmark: {name} | Data: {csv_path.name}",
            template="plotly_white",
            showlegend=False,
            bargap=0
        )

        fig.update_xaxes(title_text="Latency (ns)", row=1, col=1)
        fig.update_xaxes(title_text="Latency (ns)", row=2, col=1)
        fig.update_yaxes(title_text="Probability", range=[0, 1.0], row=1, col=1)
        fig.update_yaxes(title_text="Count", row=2, col=1)
        
        fig.write_html(str(output_html))
        print(f"   [成功] 报告已保存至: {output_html.resolve()}")

    except Exception as e:
        print(f"   [异常] 生成报告时出错: {e}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Latency benchmark report generator")
    parser.add_argument("name", type=str, help="Target benchmark name")
    args = parser.parse_args()
    
    generate_report(args.name)
