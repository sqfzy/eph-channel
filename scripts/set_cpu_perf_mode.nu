#!/usr/bin/env nu

def main [] {
    print "Starting performance optimization..."

    # 1. 检查并尝试设置 CPU Governor
    # 注意：WSL2 默认内核通常不支持此操作
    if ("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor" | path exists) {
        print "Setting CPU governor to performance..."
        do -i { sudo cpupower frequency-set --governor performance }
    } else {
        print "CPU frequency scaling not supported by current kernel (common in WSL2)."
    }

    # 2. 禁用实时调度限制 (对性能分析/采样工具采样率有帮助)
    print "Disabling RT runtime limit..."
    do -i { "-1" | sudo tee /proc/sys/kernel/sched_rt_runtime_us }

    # 3. 配置 Hugepages (用于内存密集型任务性能)
    if (which hugeadm | is-empty) {
        print "Error: 'hugeadm' not found. Please install it with: sudo pacman -S libhugetlbfs"
    } else {
        print "Configuring 1GB hugepages..."
        do -i { sudo hugeadm --pool-pages-min 1GB:1 }
    }
}
