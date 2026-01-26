#!/usr/bin/env nu

let target = "benchmark_ping_pong_ipc"

def main [
    --path: string
] {
    let path = ($path | default $"./build/linux/x86_64/release/($target)")

    # 1. 编译项目
    print $"正在编译项目 ($path)..." 
    xmake build ($target)

    if ($env.LAST_EXIT_CODE != 0) {
        print "编译失败，请检查代码。"
        return
    }

    # 2. 检查可执行文件是否存在
    if not ($path | path exists) {
        print $"错误: 找不到可执行文件 ($path)"
        return
    }

    sudo -v

    print "编译成功。"

    sudo $path

    print "生成延迟报告..."
    source ../.venv/bin/activate.nu
    python scripts/plot_latency.py shm_ipc_latency.csv shm_ipc_latency_report.html
}
