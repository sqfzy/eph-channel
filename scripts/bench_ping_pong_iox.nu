#!/usr/bin/env nu

let target = "benchmark_ping_pong_iox"

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
    print "编译成功。准备环境..."

    # 3. 启动 Iceoryx RouDi (后台)
    print "正在启动 iox-roudi..."
    job spawn { sudo iox-roudi | save -f roudi.log }
    
    # 等待 RouDi 完成初始化
    print "等待 RouDi 初始化..."
    sleep 1sec


    # 启动生产者
    print "启动生产者..."
    job spawn { sudo $path producer | save -f /tmp/iox_producer_output.log }

    # 稍微等待确保初始化
    sleep 1ms 

    # 启动消费者
    print "启动消费者..."
    sudo $path consumer

    sleep 1sec

    print "测试完成。查看生产者输出日志："
    cat /tmp/iox_producer_output.log

    print "生成延迟报告..."
    source ../.venv/bin/activate.nu
    python scripts/plot_latency.py iox_latency.csv iox_latency_report.html
}
