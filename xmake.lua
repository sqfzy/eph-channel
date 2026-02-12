set_project("eph")
set_version("1.0.0")

add_rules("mode.debug", "mode.release")
set_languages("c++23")

add_rules("plugin.compile_commands.autoupdate", { outputdir = "build" })

if is_mode("release") then
	set_optimize("fastest")
end

-----------------------------------------------------------------------------
-- 依赖管理
-----------------------------------------------------------------------------
add_requires("gtest", "tabulate", "benchmark", { optional = true })

-----------------------------------------------------------------------------
-- 核心库 (header-only)
-----------------------------------------------------------------------------
target("eph")
set_kind("headeronly")
add_includedirs("include", { public = true })
add_headerfiles("include/(eph/**.hpp)")
if is_plat("linux") then
	add_syslinks("numa", { public = true })
end
if is_mode("debug") then
	set_policy("build.sanitizer.address", true)
	set_policy("build.sanitizer.undefined", true)
	set_policy("build.sanitizer.thread", true)
	-- set_policy("build.sanitizer.memory", true)
	set_policy("build.sanitizer.undefined", true)
end
-- 相当于 -Wall -Wextra
set_warnings("all", "extra")
-- 针对 Linux (GCC/Clang) 的额外强化警告
if is_plat("linux") then
	add_cxxflags(
		"-Wshadow",   -- 警告变量遮蔽
		"-Wconversion", -- 警告可能丢失数据的隐式转换
		"-Wpedantic", -- 严格遵守 ISO C++ 标准
		"-Wlogical-op", -- 警告可疑的逻辑操作
		{ force = true }
	)
end

-----------------------------------------------------------------------------
-- examples
-----------------------------------------------------------------------------
for _, file in ipairs(os.files("examples/*.cpp")) do
	local name = path.basename(file)

	target("example_" .. name)
	set_kind("binary")
	set_group("examples")
	add_files(file)
	add_deps("eph")
end

-----------------------------------------------------------------------------
-- benchmarks
-----------------------------------------------------------------------------
for _, file in ipairs(os.files("benchmarks/**.cpp")) do
	local name = path.basename(file)

	target("bench_" .. name)
	set_kind("binary")
	set_group("benchmarks")
	add_files(file)
	add_deps("eph")
	add_packages("tabulate")
	add_packages("benchmark")
end

-----------------------------------------------------------------------------
-- tests
-----------------------------------------------------------------------------
for _, file in ipairs(os.files("tests/**.cpp")) do
	local name = path.basename(file)
	target("test_" .. name)
	set_kind("binary")
	set_group("tests")
	add_files(file)
	add_files("tests/main.cpp")
	add_deps("eph")
	add_packages("gtest")
	add_tests("default") -- 允许 xmake test 运行
end
