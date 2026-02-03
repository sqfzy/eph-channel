import numpy as np
import plotly.express as px
import plotly.graph_objects as go
from plotly.subplots import make_subplots
import polars as pl
import argparse
from scipy import stats

def plot_and_save_all(input, output):
    # 1. 读取数据 (HdrHistogram 导出格式: value_ns, count)
    df = pl.read_csv(input)
    
    # 确保按延迟值排序
    df = df.sort("value_ns")
    
    # 预计算统计量
    total_count = df["count"].sum()
    
    # 计算加权平均值
    mean_val = (df["value_ns"] * df["count"]).sum() / total_count
    
    # 计算 CDF
    df = df.with_columns(
        (pl.col("count").cum_sum() / total_count).alias("cdf")
    )
    
    # 计算过滤阈值 (P99)
    # 找到第一个 CDF >= 0.99 的 value_ns
    p99_val = df.filter(pl.col("cdf") >= 0.99)["value_ns"].head(1).item()
    df_clean = df.filter(pl.col("value_ns") <= p99_val)

    # 2. 创建 3 行 1 列的子图容器
    fig = make_subplots(
        rows=3, cols=1,
        subplot_titles=(
            "1. Cumulative Distribution (CDF)",
            "2. Latency Distribution (Log Scale - All Data)", 
            "3. Latency Body Distribution (Linear Scale - Filtered P99)"
        ),
        vertical_spacing=0.10
    )

    # --- 图表 1: CDF (替代 Time Series) ---
    # 由于没有时序数据(seq)，CDF 是最佳的替代总览图
    fig.add_trace(
        go.Scatter(x=df["value_ns"], y=df["cdf"], mode='lines', name='CDF', line=dict(width=2)),
        row=1, col=1
    )

    # --- 图表 2: Histogram (Log Scale) ---
    # 数据已聚合，使用 Bar 绘制
    fig.add_trace(
        go.Bar(x=df["value_ns"], y=df["count"], name='Full Dist', marker_color='#636EFA'),
        row=2, col=1
    )
    fig.update_yaxes(type="log", row=2, col=1) 

    # --- 图表 3: Histogram (Linear - P99 Body) ---
    fig.add_trace(
        go.Bar(
            x=df_clean["value_ns"], y=df_clean["count"], 
            name='Clean Dist', marker_color='#00CC96', opacity=0.8
        ),
        row=3, col=1
    )
    
    # 添加平均值虚线
    fig.add_vline(x=mean_val, line_dash="dash", line_color="black", annotation_text="Mean", row=3, col=1)

    # 3. 统一布局设置
    fig.update_layout(
        height=1000, 
        title_text=f"Latency Analysis (Total: {total_count})",
        showlegend=False,
        template="plotly_white",
        bargap=0 # 直方图之间无缝隙更像分布图
    )

    # 设置各轴标签
    fig.update_xaxes(title_text="Latency (ns)", row=1, col=1)
    fig.update_yaxes(title_text="Probability (CDF)", range=[0, 1.05], row=1, col=1)
    
    fig.update_xaxes(title_text="Latency (ns)", row=2, col=1)
    fig.update_yaxes(title_text="Count (Log)", row=2, col=1)
    
    fig.update_xaxes(title_text="Latency (ns)", row=3, col=1)
    fig.update_yaxes(title_text="Count", row=3, col=1)

    # 4. 显示与保存
    fig.write_html(output)
    print(f"图表已保存为 {output}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Plot Latency Analysis (HdrHistogram CSV)")   
    parser.add_argument("input", type=str, default="shm_latency_hist.csv", help="Input CSV file path")
    parser.add_argument("output", type=str, default="latency_report.html", help="Output HTML file path")

    args = parser.parse_args()
    plot_and_save_all(args.input, args.output)
