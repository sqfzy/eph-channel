import numpy as np
import plotly.express as px
import plotly.graph_objects as go
from plotly.subplots import make_subplots
import polars as pl
import argparse
from scipy import stats

def add_kde_trace(fig, data, row, col, color='purple', name='Density'):
    """
    辅助函数：计算并添加 KDE 曲线到指定的子图位置
    """
    try:
        kde = stats.gaussian_kde(data)
        x_range = np.linspace(min(data), max(data), 500)
        y_kde = kde(x_range)
        
        fig.add_trace(
            go.Scatter(x=x_range, y=y_kde, mode='lines', name=name, line=dict(color=color, width=2)),
            row=row, col=col
        )
    except Exception as e:
        print(f"KDE 计算失败: {e}")

def plot_and_save_all(input, output):
    # 1. 读取数据
    df = pl.read_csv(input)
    latency_data = df["latency_ns"].to_numpy()
    
    # 计算过滤阈值 (P99)
    upper_bound = df["latency_ns"].quantile(0.99)
    df_clean = df.filter(pl.col("latency_ns") <= upper_bound)

    # 2. 创建 3 行 1 列的子图容器
    fig = make_subplots(
        rows=3, cols=1,
        subplot_titles=(
            "1. Time Series (Raw)", 
            "2. Latency Distribution (Log Scale - All Data)", 
            "3. Latency Body Distribution (Linear Scale - Filtered P99)"
        ),
        vertical_spacing=0.08  # 调整子图之间的垂直间距
    )

    # --- 图表 1: Time Series ---
    fig.add_trace(
        go.Scatter(x=df["seq"], y=df["latency_ns"], mode='lines', name='Latency TS', line=dict(width=1)),
        row=1, col=1
    )

    # --- 图表 2: Histogram (Log Scale) ---
    # 注意：make_subplots 中 histogram 需要手动添加
    fig.add_trace(
        go.Histogram(x=df["latency_ns"], nbinsx=1000, name='Full Dist', marker_color='#636EFA'),
        row=2, col=1
    )
    fig.update_yaxes(type="log", row=2, col=1) # 设置第二张图的 Y 轴为对数

    # --- 图表 3: Histogram (Linear + KDE) ---
    fig.add_trace(
        go.Histogram(
            x=df_clean["latency_ns"], nbinsx=1000, 
            name='Clean Dist', marker_color='#00CC96', opacity=0.6,
            histnorm='probability density' # 为了配合 KDE，使用密度形式
        ),
        row=3, col=1
    )
    # 添加 KDE
    add_kde_trace(fig, df_clean["latency_ns"].to_numpy(), row=3, col=1, name='Body Density')
    # 添加平均值虚线
    mean_val = df_clean["latency_ns"].mean()
    fig.add_vline(x=mean_val, line_dash="dash", line_color="black", row=3, col=1)

    # 3. 统一布局设置
    fig.update_layout(
        # height=1200,  # 增加总高度以容纳三张图
        # width=1000,
        title_text="Shared Memory Latency Comprehensive Analysis",
        showlegend=False,
        template="plotly_white"
    )

    # 设置各轴标签
    fig.update_xaxes(title_text="Sequence ID", row=1, col=1)
    fig.update_xaxes(title_text="Latency (ns)", row=2, col=1)
    fig.update_xaxes(title_text="Latency (ns)", row=3, col=1)
    fig.update_yaxes(title_text="Latency (ns)", row=1, col=1)
    fig.update_yaxes(title_text="Count (Log)", row=2, col=1)
    fig.update_yaxes(title_text="Density", row=3, col=1)

    # 4. 显示与保存
    # fig.show()
    
    # 保存为交互式 HTML 文件
    fig.write_html(output)
    print(f"图表已保存为 {output}")

if __name__ == "__main__":
    # 第一个参数是输入路径，第二个是输出路径
    parser = argparse.ArgumentParser(description="Plot Shared Memory Latency Analysis")   
    parser.add_argument("input", type=str, default="shm_latency.csv", help="Input CSV file path")
    parser.add_argument("output", type=str, default="latency_report.html", help="Output HTML file path")

    args = parser.parse_args()
    plot_and_save_all(args.input, args.output)
