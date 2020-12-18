add_links("uv")
if is_plat("windows", "mingw", "msys") then
    add_links("boost_context-mt")
    add_syslinks("ws2_32")
end
set_warnings("all", "error")
set_languages("c++11")
add_deps("malog", "coros")

target("compute")
    set_kind("binary")
    add_files("compute.cpp")

target("echo")
    set_kind("binary")
    add_files("echo.cpp")

target("pingpong")
    set_kind("binary")
    add_files("pingpong.cpp")
