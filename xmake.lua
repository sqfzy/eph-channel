set_project("shm_channel")
set_version("1.0.0")

add_rules("mode.debug", "mode.release")

set_languages("c++23")

if is_mode("release") then
    set_optimize("fastest")
end

-----------------------------------------------------------------------------
-- 依赖管理
-----------------------------------------------------------------------------
add_requires("benchmark")
add_includedirs("/usr/include/iceoryx/v2.95.8")
add_requires("iceoryx")
add_requires("gtest")

-----------------------------------------------------------------------------
-- 核心库 (Header-only)
-----------------------------------------------------------------------------
target("shm_channel")
set_kind("headeronly")
-- 导出接口：让所有 add_deps(xxx) 的目标自动获得这些配置
add_includedirs("include", { public = true })
add_headerfiles("include/(shm_channel/*.hpp)")


-----------------------------------------------------------------------------
-- Examples
-----------------------------------------------------------------------------
for _, file in ipairs(os.files("examples/*.cpp")) do
    -- 从路径中提取文件名（不带后缀），例如 "examples/ipc_channel.cpp" -> "ipc_channel"
    local name = path.basename(file)

    target("example_" .. name)
    set_kind("binary")
    set_group("examples")
    add_files(file)
    add_deps("shm_channel")
    add_syslinks("pthread")
end

-----------------------------------------------------------------------------
-- Benchmarks
-----------------------------------------------------------------------------
if is_mode("release") then
    for _, file in ipairs(os.files("benchmark/examples/*.cpp")) do
        local name = path.basename(file)

        target("benchmark_" .. name)
        set_kind("binary")
        set_group("benchmarks")
        add_files(file)
        add_deps("shm_channel")
        add_includedirs("benchmark/include", "benchmark/examples")

        if name:find("iox") then
            add_packages("iceoryx")
        else
            add_packages("benchmark")
        end

        add_syslinks("pthread")
    end
end

-----------------------------------------------------------------------------
-- Tests
-----------------------------------------------------------------------------
for _, file in ipairs(os.files("tests/test_*.cpp")) do
    local name = path.basename(file)
    target("test_" .. name)
    set_kind("binary")
    set_group("tests")
    add_files(file)
    add_files("tests/main.cpp")
    add_deps("shm_channel")
    add_packages("gtest")
    set_default(false)
    add_tests("default") -- 允许 xmake test 运行
end
