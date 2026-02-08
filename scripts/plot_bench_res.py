import polars as pl
import plotly.express as px

# 1. 结构化原始数据
# 定义格式：(场景, 数据规模, 缓存大小, 耗时ns)
raw_data = [
    # Baseline
    ("baseline", "64B", "2B", 373.39), ("baseline", "64B", "8B", 341.21), ("baseline", "64B", "32B", 351.47), ("baseline", "64B", "64B", 339.01), ("baseline", "64B", "512B", 363.18), ("baseline", "64B", "4096B", 397.62),
    ("baseline", "256B", "2B", 489.58), ("baseline", "256B", "8B", 419.05), ("baseline", "256B", "32B", 458.09), ("baseline", "256B", "64B", 461.68), ("baseline", "256B", "512B", 472.02), ("baseline", "256B", "4096B", 531.43),
    ("baseline", "1024B", "2B", 700.76), ("baseline", "1024B", "8B", 568.86), ("baseline", "1024B", "32B", 606.78), ("baseline", "1024B", "64B", 553.31), ("baseline", "1024B", "512B", 596.08), ("baseline", "1024B", "4096B", 623.13),
    
    # Cache Line
    ("cache_line", "64B", "2B", 376.77), ("cache_line", "64B", "8B", 341.30), ("cache_line", "64B", "32B", 349.41), ("cache_line", "64B", "64B", 345.57), ("cache_line", "64B", "512B", 368.84), ("cache_line", "64B", "4096B", 411.35),
    ("cache_line", "256B", "2B", 499.48), ("cache_line", "256B", "8B", 418.65), ("cache_line", "256B", "32B", 455.48), ("cache_line", "256B", "64B", 471.99), ("cache_line", "256B", "512B", 480.47), ("cache_line", "256B", "4096B", 535.08),
    ("cache_line", "1024B", "2B", 679.18), ("cache_line", "1024B", "8B", 597.34), ("cache_line", "1024B", "32B", 563.11), ("cache_line", "1024B", "64B", 533.94), ("cache_line", "1024B", "512B", 593.24), ("cache_line", "1024B", "4096B", 588.54),

    # 取模索引
    ("modulo", "64B", "2B", 420.49), ("modulo", "64B", "8B", 655.02), ("modulo", "64B", "32B", 659.01), ("modulo", "64B", "64B", 642.27), ("modulo", "64B", "512B", 725.00), ("modulo", "64B", "4096B", 962.64),
    ("modulo", "256B", "2B", 908.97), ("modulo", "256B", "8B", 817.33), ("modulo", "256B", "32B", 843.79), ("modulo", "256B", "64B", 940.58), ("modulo", "256B", "512B", 964.93), ("modulo", "256B", "4096B", 1022.46),
    ("modulo", "1024B", "2B", 1041.56), ("modulo", "1024B", "8B", 1089.57), ("modulo", "1024B", "32B", 1076.37), ("modulo", "1024B", "64B", 1112.62), ("modulo", "1024B", "512B", 1219.62), ("modulo", "1024B", "4096B", 1183.13),

    # Shadow Index
    ("shadow", "64B", "2B", 686.93), ("shadow", "64B", "8B", 665.27), ("shadow", "64B", "32B", 617.40), ("shadow", "64B", "64B", 713.17), ("shadow", "64B", "512B", 865.94), ("shadow", "64B", "4096B", 831.73),
    ("shadow", "256B", "2B", 795.66), ("shadow", "256B", "8B", 698.02), ("shadow", "256B", "32B", 908.44), ("shadow", "256B", "64B", 902.90), ("shadow", "256B", "512B", 1054.12), ("shadow", "256B", "4096B", 954.42),
    ("shadow", "1024B", "2B", 1120.83), ("shadow", "1024B", "8B", 1119.08), ("shadow", "1024B", "32B", 1093.69), ("shadow", "1024B", "64B", 942.61), ("shadow", "1024B", "512B", 1009.34), ("shadow", "1024B", "4096B", 540.41),
]

# 2. 数据处理
df = pl.DataFrame(raw_data, schema=["Scenario", "DataSize", "BufSize", "Latency_ns"])

# 提取 BufSize 中的数字用于物理排序 (避免 32B 排在 8B 前面)
df = df.with_columns(
    pl.col("BufSize").str.extract(r"(\d+)", 1).cast(pl.Int64).alias("Buf_int")
).sort(["Scenario", "DataSize", "Buf_int"])

# 3. 绘图：多维度对比
fig = px.line(
    df,
    x="BufSize",
    y="Latency_ns",
    color="DataSize",
    facet_col="Scenario",
    facet_col_wrap=2,
    markers=True,
    title="Benchmark Result Comparison (Lower is Better)",
    labels={"Latency_ns": "Latency (ns)", "BufSize": "Buffer Size"},
    template="plotly_dark" # 深色模式适合阅读代码
)

# 统一 X 轴排序并优化显示
fig.update_xaxes(categoryorder="array", categoryarray=df.sort("Buf_int")["BufSize"].unique().to_list())
fig.show()
