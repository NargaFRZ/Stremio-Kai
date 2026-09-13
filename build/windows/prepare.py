"""Apply only the Discord changes to the pinned, existing native application.

The Windows entry point performs the release EXE identity check before calling
this script. This script can also be run on Linux to review/test the source diff.
"""
import argparse
import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[2]
PINS = json.loads((Path(__file__).parent / "pins.json").read_text())


def git(path, *args):
    return subprocess.check_output(["git", "-C", str(path), *args], text=True).strip()


def replace_once(path, old, new):
    text = path.read_text(encoding="utf-8")
    if text.count(old) != 1:
        raise RuntimeError(f"Unexpected source layout in {path}: {old!r}")
    path.write_text(text.replace(old, new), encoding="utf-8", newline="\n")


def prepare(source, rpc):
    for key, path in (("native", source), ("rpc", rpc)):
        if git(path, "rev-parse", "HEAD") != PINS[key]["commit"]:
            raise RuntimeError(f"{key}: checkout does not match pins.json")
        if git(path, "diff", "HEAD", "--"):
            raise RuntimeError(f"{key}: use a clean checkout before preparing")

    patch = ROOT / "build/discord/discord-rpc.patch"
    subprocess.run(["git", "-C", str(rpc), "apply", "--check", str(patch)], check=True)
    subprocess.run(["git", "-C", str(rpc), "apply", str(patch)], check=True)
    for name in ("discord.cpp", "discord.h", "presence.h"):
        (source / "src/utils" / name).write_bytes((ROOT / "build/discord" / name).read_bytes())
    (source / "tests").mkdir(exist_ok=True)
    for name in ("discord-presence.cpp", "discord-rpc-reconnect.cpp"):
        (source / "tests" / name).write_bytes((ROOT / "tests" / name).read_bytes())

    cleanup = source / "src/utils/crashlog.cpp"
    replace_once(cleanup, '#include "discord_rpc.h"', '#include "discord_rpc.h"\n#include "discord.h"')
    replace_once(cleanup, "    Discord_Shutdown();",
                 "    ShutdownDiscordPresenceScheduler();\n    Discord_Shutdown();")

    cmake = source / "CMakeLists.txt"
    for arch in ("win64-static", "win32-static"):
        replace_once(cmake, f'"${{CMAKE_CURRENT_SOURCE_DIR}}/deps/discord-rpc/{arch}/lib/discord-rpc.lib"',
                     "discord-rpc")
        replace_once(cmake, f'"${{CMAKE_CURRENT_SOURCE_DIR}}/deps/discord-rpc/{arch}/include"',
                     '"${CMAKE_CURRENT_SOURCE_DIR}/deps/discord-rpc/include"')
    replace_once(cmake, "include_directories(${DISCORD_INCLUDE_DIR})", """
# Compile the patched SDK instead of using the unavailable prebuilt RPC library.
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(USE_STATIC_CRT ON CACHE BOOL "" FORCE)
set(CLANG_FORMAT_SUFFIX none CACHE STRING "" FORCE)
set(RAPIDJSON "${CMAKE_CURRENT_SOURCE_DIR}/deps/discord-rpc/thirdparty/rapidjson" CACHE FILEPATH "" FORCE)
set(RAPIDJSONTEST "${RAPIDJSON}" CACHE FILEPATH "" FORCE)
add_subdirectory(deps/discord-rpc)
set_property(TARGET discord-rpc PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# Preserve Kai's shipped MPV runtime; the import library/headers use the base pin.
if(NOT KAI_RUNTIME_MPV OR NOT EXISTS "${KAI_RUNTIME_MPV}")
    message(FATAL_ERROR "Set KAI_RUNTIME_MPV to the verified Kai portable libmpv-2.dll")
endif()
set(MPV_DLL "${KAI_RUNTIME_MPV}")
include_directories(${DISCORD_INCLUDE_DIR})""")
    with cmake.open("a", encoding="utf-8", newline="\n") as handle:
        handle.write("""

enable_testing()
add_executable(kai_presence_tests
    tests/discord-presence.cpp src/utils/discord.cpp
    deps/discord-rpc/src/serialization.cpp)
target_include_directories(kai_presence_tests PRIVATE
    src/utils deps/discord-rpc/include deps/discord-rpc/src
    deps/discord-rpc/thirdparty/rapidjson/include)
target_link_libraries(kai_presence_tests PRIVATE user32)
if(MSVC)
    target_compile_options(stremio PRIVATE /utf-8)
    target_compile_options(kai_presence_tests PRIVATE /utf-8)
endif()
add_test(NAME discord_presence COMMAND kai_presence_tests)

add_executable(kai_rpc_reconnect_tests tests/discord-rpc-reconnect.cpp
    deps/discord-rpc/src/discord_rpc.cpp deps/discord-rpc/src/serialization.cpp)
target_include_directories(kai_rpc_reconnect_tests PRIVATE
    deps/discord-rpc/include deps/discord-rpc/src
    deps/discord-rpc/thirdparty/rapidjson/include)
target_compile_definitions(kai_rpc_reconnect_tests PRIVATE DISCORD_DISABLE_IO_THREAD)
if(MSVC)
    target_compile_options(kai_rpc_reconnect_tests PRIVATE /utf-8)
endif()
add_test(NAME discord_rpc_reconnect COMMAND kai_rpc_reconnect_tests)
""")
    (source / "vcpkg.json").write_text(json.dumps({
        "name": "stremio-kai-discord-build", "version-string": "4.8.0-rpc1",
        "dependencies": ["curl", "nlohmann-json", "openssl", "webview2"]
    }, indent=2) + "\n", encoding="utf-8")
    print("Prepared pinned Community 5.0.21 source with the Discord-only patch.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    options = parser.parse_args()
    source = options.source.resolve()
    prepare(source, source / "deps/discord-rpc")
