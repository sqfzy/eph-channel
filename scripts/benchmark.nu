#!/usr/bin/env nu

# --- 配置 ---
const VALID_BENCHMARKS = [
    "ping_pong_itc", 
    "ping_pong_ipc", 
    "ping_pong_iox", 
    "ping_pong_udp"
]

# --- 主函数 ---
def main [...targets: string] {
    # 1. 定义路径变量
    let base_dir = ($env.CURRENT_FILE | path dirname | path dirname)
    let output_dir = ($base_dir | path join "outputs")
    let build_dir = ($base_dir | path join "build/linux/x86_64/release")

    # 2. 确定运行目标
    let run_list = if ($targets | is-empty) {
        $VALID_BENCHMARKS
    } else {
        let invalid = ($targets | where { |t| $t not-in $VALID_BENCHMARKS })
        if not ($invalid | is-empty) {
            error make { msg: $"❌ 错误: 未知的测试目标: ($invalid)\n可选目标: ($VALID_BENCHMARKS | str join ', ')" }
        }
        $targets
    }

    print $"🚀 开始执行基准测试: ($run_list | str join ', ')\n"
    sudo -v # 预先获取权限

    # 3. 循环执行 (注意：这里显式传递了 build_dir 和 output_dir)
    for name in $run_list {
        run-single-bench $name $build_dir $output_dir
    }

    # 4. 汇总结果
    print "\n📊 [汇总] 正在生成测试报告..."
    print "------------------------------------------"
    # 这里也显式传递了 output_dir
    print ($run_list | each { |name| get-test-data $name $output_dir } | table --expand)
    print "------------------------------------------"
    print "\n✅ 所有任务已完成"
}

# --- 单个任务执行逻辑
def run-single-bench [name: string, build_dir: string, output_dir: string] {
    # === 动态推导配置 ===
    let target_bin = $"benchmark_($name)"
    let needs_roudi = ($name == "ping_pong_iox")
    let exec_path = ($build_dir | path join $target_bin)
    
    print $"👉 [($name)] 准备中..."

    # A. 编译
    print $"   [编译] 正在构建 ($target_bin)..."
    xmake build $target_bin
    
    if ($env.LAST_EXIT_CODE != 0) {
        print $"❌ [失败] 编译错误"
        return
    }

    if not ($exec_path | path exists) {
        print $"❌ [失败] 找不到可执行文件: ($exec_path)"
        return
    }

    # B. 环境准备 (根据名称自动判断是否启动 RouDi)
    if $needs_roudi {
        print "   [环境] 启动 iox-roudi..."
        job spawn { sudo iox-roudi | save -f roudi.log }
        sleep 1sec # 等待初始化
    }

    # C. 运行
    print $"   [运行] 执行基准测试..."
    try {
        sudo $exec_path
    } catch {
        print $"❌ [错误] 运行时异常"
    }

    # D. 清理
    if $needs_roudi {
        print "   [清理] 停止 iox-roudi..."
        try { sudo pkill -x iox-roudi }
    }

    # E. 绘图 (Python)
    print "   [报告] 生成延迟图表..."
    let csv_path = ($output_dir | path join $"($name)_latency.csv")
    let html_path = ($output_dir | path join $"($name)_latency_report.html")

    do {
        if ("../.venv/bin/activate.nu" | path exists) {
            source ../.venv/bin/activate.nu
        }
        if ($csv_path | path exists) {
            python scripts/plot_latency.py $csv_path $html_path
        }
    }
    
    print $"✅ [完成] ($name)\n"
}

# --- 数据读取辅助函数
def get-test-data [name: string, output_dir: string] {
    let pattern = $"($output_dir)/bench_($name)_latency*.json"
    let latest = (glob $pattern | ls ...$in | sort-by modified | last)

    if ($latest | is-empty) {
        return { test_name: $name, status: "❌ NOT FOUND", stats: null, date: null }
    }

    let data = (open $latest.name)
    {
        test_name: $name,
        status: "✅ SUCCESS",
        stats: $data.stats,
        date: $data.date
    }
}
