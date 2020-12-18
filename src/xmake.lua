add_rules("mode.debug", "mode.release")

target("coros")
    set_kind("static")

    add_files("coroutine.cpp")
    add_files("scheduler.cpp")
    add_files("socket.cpp")

    set_warnings("all", "error")
    set_languages("c++11")
    add_includedirs("../include", {public = true})
    on_load(function(target)
	target:add(find_packages("boost"))
    end)
