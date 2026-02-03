set_project("eph")
set_version("1.0.0")


add_rules("mode.debug", "mode.release")
set_languages("c++23")

if is_mode("release") then
    set_optimize("fastest")
end

-----------------------------------------------------------------------------
-- 依赖管理
-----------------------------------------------------------------------------
-- vcpkg 2.** 版本太低，需要最新版
-- add_includedirs("/usr/include/iceoryx/v2.95.8")
add_requires("gtest")
-- add_requires("iceoryx")
if is_plat("linux") then
    add_syslinks("numa")
end

-----------------------------------------------------------------------------
-- 核心库 (header-only)
-----------------------------------------------------------------------------
target("eph")
set_kind("headeronly")
add_includedirs("include", { public = true })
add_headerfiles("include/(eph/**.hpp)")

-----------------------------------------------------------------------------
-- examples
-----------------------------------------------------------------------------
for _, file in ipairs(os.files("examples/*.cpp")) do
    -- 从路径中提取文件名（不带后缀），例如 "examples/ipc_channel.cpp" -> "ipc_channel"
    local name = path.basename(file)

    target("example_" .. name)
    set_kind("binary")
    set_group("examples")
    set_default(false)
    add_files(file)
    add_deps("eph")
    add_syslinks("pthread")
end

-----------------------------------------------------------------------------
-- benchmarks
-----------------------------------------------------------------------------
if is_mode("release") then
    for _, file in ipairs(os.files("benchmarks/*.cpp")) do
        local name = path.basename(file)

        target("benchmark_" .. name)
        set_kind("binary")
        set_group("benchmarks")
        set_default(false)
        add_files(file)
        add_deps("eph")

        -- if name:find("iox") then
        --     add_packages("iceoryx")
        -- end

        add_syslinks("pthread")
    end
end

-----------------------------------------------------------------------------
-- tests
-----------------------------------------------------------------------------
for _, file in ipairs(os.files("tests/**/*.cpp")) do
    local name = path.basename(file)
    target("test_" .. name)
    set_kind("binary")
    set_group("tests")
    add_files(file)
    add_files("tests/main.cpp")
    add_deps("eph")
    add_packages("gtest")
    set_default(false)
    add_tests("default") -- 允许 xmake test 运行
end

package("eph")
    set_kind("library", {headeronly = true})
    set_description("High-frequency trading C++ primitive")

    set_urls("https://github.com/sqfzy/ephemeral.git")
    add_versions("1.0", "9e317e3")

    on_install(function (package)
        local configs = {}
        if package:config("shared") then
            configs.kind = "shared"
        end
        import("package.tools.xmake").install(package, configs)
    end)

    on_test(function (package)
        package:check_cxxsnippets({test = [[
            #include <eph/platform.hpp>
            void test() {
                eph::cpu_relax();
            }
        ]]}, {configs = {languages = "c++23"}})
    end)
