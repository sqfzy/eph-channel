xmake build --group=benchmarks

let tasks = [
    { cmd: "bench_ring_buffer_aaod", out: "./outputs/res.txt" },
    { cmd: "bench_seqlock_ring_buffer1_aaod", out: "./outputs/res1.txt" },
]

# 并行执行
$tasks | par-each {|task|
    # 执行命令并保存
    xmake run $task.cmd | save -f -a $task.out
}


let tasks = [
    { cmd: "bench_seqlock_ring_buffer2_aaod", out: "./outputs/res2.txt" },
    { cmd: "bench_seqlock_ring_buffer3_aaod", out: "./outputs/res3.txt" },
]

# 并行执行
$tasks | par-each {|task|
    # 执行命令并保存
    xmake run $task.cmd | save -f -a $task.out
}

let tasks = [
    { cmd: "bench_seqlock_ring_buffer4_aaod", out: "./outputs/res4.txt" }
]

# 并行执行
$tasks | par-each {|task|
    # 执行命令并保存
    xmake run $task.cmd | save -f -a $task.out
}
