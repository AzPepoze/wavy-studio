set_project("wavy-studio")
set_version("0.1.0")
set_languages("cxx20")
set_warnings("all", "extra")
set_config("qt_sdkver", "6.11.2", {force = false})
add_rules("mode.debug", "mode.release")
option("asio")
    set_default(false)
    set_showmenu(true)
    set_description("Enable RtAudio ASIO backend")
option_end()
option("jack")
    set_default(is_plat("linux"))
    set_showmenu(true)
    set_description("Enable RtAudio JACK backend (needs libjack development files)")
option_end()

-- ASIO is available under GPLv3 (this project is GPL-3.0-or-later), so binaries may include it.
-- Shared on Linux so the package's own link test finds ALSA/Pulse/JACK; static on Windows so no DLL must be found at run time.
-- Windows also uses the dynamic C runtime (MD) like Qt, otherwise the linker reports a RuntimeLibrary mismatch.
local audio_configs = {asio = has_config("asio"), shared = is_plat("linux")}
if is_plat("windows") then
    set_runtimes("MD")
    audio_configs.runtimes = "MD"
end
if is_plat("linux") then
    audio_configs.alsa = true
    audio_configs.pulseaudio = true
    audio_configs.jack = has_config("jack")
elseif is_plat("windows", "mingw") then
    audio_configs.wasapi = true
    audio_configs.direct_sound = true
end
-- Prefer a system RtAudio on Linux (instant, uses the distro's JACK/ALSA/Pulse); otherwise
-- xmake builds RtAudio 6.0.1 from source.
local system_rtaudio = false
if is_plat("linux") then
    for _, dir in ipairs({"/usr/lib", "/usr/lib64", "/usr/lib/x86_64-linux-gnu", "/usr/share", "/usr/local/lib"}) do
        if os.isfile(path.join(dir, "pkgconfig", "rtaudio.pc")) then
            system_rtaudio = true
        end
    end
end
if system_rtaudio then
    add_requires("pkgconfig::rtaudio", {alias = "rtaudio"})
else
    add_requires("rtaudio 6.0.1", {system = false, configs = audio_configs})
end
add_requires("doctest")

-- Override with xmake f --qt=<SDK directory> for Windows or another Qt SDK.
if is_plat("linux") then
    set_config("qt", "/usr", {force = false})
end

-- A shared RtAudio built by xmake lives in its package dir, so executables need an rpath to find it.
rule("rtaudio_rpath")
    on_load(function (target)
        local pkg = target:pkg("rtaudio")
        if pkg then
            for _, dir in ipairs(pkg:get("linkdirs") or {}) do
                target:add("rpathdirs", dir)
            end
        end
    end)

target("wavy_engine")
    set_kind("static")
    add_files("src/engine/AudioEngine.cpp", "src/engine/Log.cpp")
    add_includedirs("src/engine", {public = true})
    add_packages("rtaudio", {public = true})
    if is_plat("linux") then
        add_syslinks("pthread", "dl", "m", {public = true})
    end

target("wavy-studio")
    add_rules("qt.quickapp", "rtaudio_rpath")
    add_frameworks("QtQuickControls2")
    add_deps("wavy_engine")
    add_packages("rtaudio")
    add_files("src/app/main.cpp", "src/app/EngineController.hpp", "src/ui/ui.qrc")

target("engine_tests")
    set_kind("binary")
    add_deps("wavy_engine")
    add_rules("rtaudio_rpath")
    add_packages("doctest", "rtaudio")
    add_files("tests/engine_test.cpp")
    add_tests("no_device", {run_timeout = 10})

task("fmt")
    set_menu {
        usage = "xmake fmt",
        description = "Format C++ sources under src/ and tests/ with clang-format"
    }
    on_run(function ()
        local files = table.join(
            os.files("src/**.cpp"), os.files("src/**.hpp"),
            os.files("tests/**.cpp"), os.files("tests/**.hpp"))
        os.execv("clang-format", table.join("-i", files))
    end)

task("lint")
    set_menu {
        usage = "xmake lint",
        description = "Run qmllint on src/ui/main.qml"
    }
    on_run(function ()
        import("lib.detect.find_tool")
        local qmllint = find_tool("qmllint", {paths = "/usr/lib/qt6/bin"})
        assert(qmllint, "qmllint not found; install Qt 6 or add its bin directory to PATH")
        os.execv(qmllint.program, {"src/ui/main.qml"})
    end)
