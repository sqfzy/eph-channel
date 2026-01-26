#!/usr/bin/env nu

# 仅负责查找并读取数据
def get-test-data [name: string, output_dir: string] {
    let pattern = $"($output_dir)/($name)_*.json"
    
    # 1. 尝试获取最新的文件路径
    let latest = (glob $pattern | ls ...$in | sort-by modified | last)

    # 2. 如果没找到文件，直接返回错误结构
    if ($latest | is-empty) {
        return { test_name: $name, status: "❌ NOT FOUND", stats: null }
    }

    # 3. 读取并转换数据
    let data = (open $latest.name)
    {
        test_name: $name,
        status: "✅ SUCCESS",
        stats: $data.stats,
        date: $data.date
    }
}

# --- 配置 ---
let scripts = ["bench_ping_pong_itc", "bench_ping_pong_ipc", "bench_ping_pong_iox"]
let base_dir = ($env.CURRENT_FILE | path dirname)
let output_dir = ($base_dir | path join "../outputs")

# --- 第一阶段：纯执行 ---
print "🚀 [阶段 1/2] 开始执行基准测试脚本..."

for name in $scripts {
    let script_path = ($base_dir | path join $"($name).nu")
    
    if ($script_path | path exists) {
        print $"正在运行: ($name)..."
        ^nu $script_path --output_dir $output_dir
    } else {
        print $"❌ 跳过: 找不到脚本 ($script_path)"
    }

}

# --- 第二阶段：纯打印 ---
print "\n📊 [阶段 2/2] 正在汇总测试结果..."

# 遍历脚本列表，去对应的目录抓取最新的 JSON
let summary_table = ($scripts | each { |name|
    get-test-data $name $output_dir
})

# 最终统一输出表格
print "------------------------------------------"
print ($summary_table | table --expand)
print "------------------------------------------"

print "\n✅ 所有任务已完成"
