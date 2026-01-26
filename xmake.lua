add_rules("mode.debug", "mode.release")

add_requires("tracy", "iceoryx", "gtest")
set_languages("c++23")
set_optimize("fastest")
--  显式生成调试符号 (相当于 -g)
set_symbols("debug")
-- 禁止在链接阶段剥离符号 (防止 strip 移除 debug info)
set_strip("none")
add_cxflags("-march=native", "-Wall", "-pthread")

add_includedirs("/usr/include/iceoryx/v2.95.8")




-- add_defines("TRACY_ENABLE") -- 开启 Tracy 内部的开关宏

target("shm-ring")
    set_kind("static")
    add_includedirs("include", {public = true})

target("benchmark")
    set_kind("static")
    add_includedirs("benchmark/include", {public = true})


target("examples_simple")
    set_kind("binary")
    add_files("examples/simple.cpp")
    add_deps("shm-ring")

target("examples_duplex")
    set_kind("binary")
    add_files("examples/duplex.cpp")
    add_deps("shm-ring")

target("benchmark_ping_pong_ipc")
    set_kind("binary")
    add_files("benchmark/examples/ping_pong_ipc.cpp")
    add_deps("shm-ring")
    add_deps("benchmark")

target("benchmark_ping_pong_itc")
    set_kind("binary")
    add_files("benchmark/examples/ping_pong_itc.cpp")
    add_deps("shm-ring")
    add_deps("benchmark")

target("benchmark_ping_pong_iox")
    set_kind("binary")
    add_files("benchmark/examples/ping_pong_iox.cpp")
    add_packages("iceoryx")
    add_deps("shm-ring")
    add_deps("benchmark")

target("unit_tests")
    set_kind("binary")
    set_default(false) -- 默认不构建，除非显式指定或运行 xmake test
    add_includedirs("include")
    add_files("tests/*.cpp")
    add_packages("gtest")
    add_deps("shm-ring")
    add_syslinks("pthread", "rt") 
    -- 定义一个简单的运行规则
    on_run(function (target)
        os.exec(target:targetfile())
    end)

