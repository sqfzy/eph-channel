add_rules("mode.debug", "mode.release")

add_requires("tracy", "iceoryx")
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

target("benchmark_ping_pong")
    set_kind("binary")
    add_files("benchmark/examples/ping_pong.cpp")
    add_deps("shm-ring")
    add_deps("benchmark")

target("benchmark_iox_ping_pong")
    set_kind("binary")
    add_files("benchmark/examples/iox_ping_pong.cpp")
    add_packages("iceoryx")
    add_deps("benchmark")

-- target("shm-main")
--     set_kind("binary")
--     add_files("src/shm_main.cpp")
--     add_packages("tracy")
--     add_syslinks("rt", "pthread") 
--
-- target("iox-main")
--     set_kind("binary")
--     add_files("src/iox_main.cpp")
--     add_packages("iceoryx")
--     add_syslinks("pthread", "rt")


--
-- If you want to known more usage about xmake, please see https://xmake.io
--
-- ## FAQ
--
-- You can enter the project directory firstly before building project.
--
--   $ cd projectdir
--
-- 1. How to build project?
--
--   $ xmake
--
-- 2. How to configure project?
--
--   $ xmake f -p [macosx|linux|iphoneos ..] -a [x86_64|i386|arm64 ..] -m [debug|release]
--
-- 3. Where is the build output directory?
--
--   The default output directory is `./build` and you can configure the output directory.
--
--   $ xmake f -o outputdir
--   $ xmake
--
-- 4. How to run and debug target after building project?
--
--   $ xmake run [targetname]
--   $ xmake run -d [targetname]
--
-- 5. How to install target to the system directory or other output directory?
--
--   $ xmake install
--   $ xmake install -o installdir
--
-- 6. Add some frequently-used compilation flags in xmake.lua
--
-- @code
--    -- add debug and release modes
--    add_rules("mode.debug", "mode.release")
--
--    -- add macro definition
--    add_defines("NDEBUG", "_GNU_SOURCE=1")
--
--    -- set warning all as error
--    set_warnings("all", "error")
--
--    -- set language: c99, c++11
--    set_languages("c99", "c++11")
--
--    -- set optimization: none, faster, fastest, smallest
--    set_optimize("fastest")
--
--    -- add include search directories
--    add_includedirs("/usr/include", "/usr/local/include")
--
--    -- add link libraries and search directories
--    add_links("tbox")
--    add_linkdirs("/usr/local/lib", "/usr/lib")
--
--    -- add system link libraries
--    add_syslinks("z", "pthread")
--
--    -- add compilation and link flags
--    add_cxflags("-stdnolib", "-fno-strict-aliasing")
--    add_ldflags("-L/usr/local/lib", "-lpthread", {force = true})
--
-- @endcode
--

