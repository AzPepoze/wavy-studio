set_project("wavy-studio")
set_version("0.1.0")
-- C++23 for the whole project: std::expected is used in public engine headers.
set_languages("cxx23")
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
-- Shared everywhere: on Linux the package's own link test needs it to find ALSA/Pulse/JACK, and on Windows a
-- static RtAudio is built with the static C runtime (MT), which clashes with Qt's dynamic one (MD).
-- Windows also uses the dynamic C runtime (MD) like Qt, otherwise the linker reports a RuntimeLibrary mismatch.
local audio_configs = {asio = has_config("asio"), shared = true}
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
add_requires("miniaudio")

-- Override with xmake f --qt=<SDK directory> for Windows or another Qt SDK.
if is_plat("linux") then
    set_config("qt", "/usr", {force = false})
end

-- A shared RtAudio built by xmake lives in its package dir, so executables must be able to find it at run time:
-- an rpath on Linux, and a copy of the DLL next to the executable on Windows.
rule("rtaudio_rpath")
    on_load(function (target)
        local pkg = target:pkg("rtaudio")
        if pkg then
            for _, dir in ipairs(pkg:get("linkdirs") or {}) do
                target:add("rpathdirs", dir)
            end
        end
    end)
    after_build(function (target)
        local pkg = target:pkg("rtaudio")
        if pkg and target:is_plat("windows", "mingw") then
            for _, dll in ipairs(os.files(path.join(pkg:installdir(), "bin", "*.dll"))) do
                os.cp(dll, target:targetdir())
            end
        end
    end)

-- miniaudio's implementation is big, so it lives in its own library and is compiled once.
target("wavy_decoder")
    set_kind("static")
    add_files("src/engine/io/MiniaudioDecoder.cpp")
    add_includedirs("src/engine")
    add_packages("miniaudio")

target("wavy_engine")
    set_kind("static")
    add_files("src/engine/**.cpp|io/MiniaudioDecoder.cpp")
    add_deps("wavy_decoder")
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
    add_files("src/app/main.cpp", "src/app/EngineController.hpp", "src/app/TimelineModel.cpp", "src/app/TimelineModel.hpp", "src/ui/ui.qrc")

target("engine_tests")
    set_kind("binary")
    add_deps("wavy_engine")
    add_rules("rtaudio_rpath")
    add_packages("doctest", "rtaudio")
    add_files("tests/audio/engine_test.cpp")
    add_tests("no_device")

target("audio_file_tests")
    set_kind("binary")
    add_deps("wavy_engine")
    add_rules("rtaudio_rpath")
    add_packages("doctest", "rtaudio")
    add_files("tests/io/audio_file_test.cpp")
    add_tests("audio_file")

target("timeline_tests")
    set_kind("binary")
    add_deps("wavy_engine")
    add_rules("rtaudio_rpath")
    add_packages("doctest", "rtaudio")
    add_files("tests/timeline/timeline_test.cpp")
    add_tests("timeline")

target("timeline_model_tests")
    set_kind("binary")
    add_rules("qt.console", "rtaudio_rpath")
    add_frameworks("QtCore")
    add_deps("wavy_engine")
    add_packages("doctest", "rtaudio")
    add_files("src/app/TimelineModel.cpp", "src/app/TimelineModel.hpp", "tests/app/timeline_model_test.cpp")
    add_includedirs("src/app")
    add_tests("timeline_model")

target("mixer_tests")
    set_kind("binary")
    add_deps("wavy_engine")
    add_rules("rtaudio_rpath")
    add_packages("doctest", "rtaudio")
    add_files("tests/audio/mixer_test.cpp")
    add_tests("mixer")

target("ui_tests")
    set_kind("phony")
    add_tests("timeline")
    on_test(function (target)
        import("core.project.config")
        import("lib.detect.find_tool")
        import("detect.sdks.find_qt")
        local qt = find_qt(config.get("qt"), {version = config.get("qt_sdkver")})
        local paths = {"/usr/lib/qt6/bin"}
        if qt and qt.bindir then
            table.insert(paths, 1, qt.bindir)
        end
        local runner = find_tool("qmltestrunner", {paths = paths, norun = true, force = true})
        if not runner then
            -- CI sets WAVY_REQUIRE_UI_TESTS so a missing runner fails instead of silently skipping the tests.
            assert(not os.getenv("WAVY_REQUIRE_UI_TESTS"), "qmltestrunner not found but UI tests are required")
            print("Skipping UI tests: qmltestrunner not found; install Qt's test tools or add the Qt bin directory to PATH")
            return true
        end
        os.execv(runner.program, {"-input", path.join(os.projectdir(), "tests", "ui")}, {
            envs = {QT_QPA_PLATFORM = "offscreen", QT_QUICK_BACKEND = "software", QT_QPA_PLATFORMTHEME = ""}
        })
        return true
    end)

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

target("effects_tests")
    set_kind("binary")
    add_deps("wavy_engine")
    add_rules("rtaudio_rpath")
    add_packages("doctest", "rtaudio")
    add_files("tests/effects/effects_test.cpp")
    add_tests("effects")
