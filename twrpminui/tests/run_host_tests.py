#!/usr/bin/env python3
"""Run graphics and input regression checks without an Android build or device.

Requires a host C++17 compiler and libpng16.so.16. Uses this tree's libpng headers.
"""
from pathlib import Path
import os
import subprocess
import tempfile

recovery = Path(__file__).resolve().parents[2]
android = recovery.parents[1]
with tempfile.TemporaryDirectory(prefix="twrp-graphics-test-") as directory:
    temporary = Path(directory)
    (temporary / "host_compat.h").write_text(
        '#include <pthread.h>\n#include <sys/stat.h>\n'
        '#ifndef __unused\n#define __unused __attribute__((unused))\n#endif\n'
    )
    (temporary / "android-base").mkdir()
    (temporary / "android-base/properties.h").write_text(
        '#pragma once\n#include <string>\nnamespace android::base {\n'
        'std::string GetProperty(const std::string&, const std::string&);\n'
        'template<class T> T GetIntProperty(const std::string& key, T fallback) { '
        'auto s = GetProperty(key, \"\"); return s.empty() ? fallback : static_cast<T>(std::stoll(s)); }\n'
        'inline bool GetBoolProperty(const std::string&, bool fallback) { return fallback; }\n}\n'
    )
    (temporary / "android-base/strings.h").write_text(
        '#pragma once\n#include <string_view>\nnamespace android::base {\n'
        'inline bool StartsWith(std::string_view s, std::string_view p) { '
        'return s.substr(0, p.size()) == p; }\n}\n'
    )
    binary = temporary / "graphics_test"
    subprocess.run([
        os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-sign-compare", "-Wno-attributes", "-Wno-ignored-attributes",
        "-include", str(temporary / "host_compat.h"),
        "-DOVERSCAN_PERCENT=0", "-DTW_ROTATION=0",
        "-I" + str(temporary), "-I" + str(recovery / "twrpminui/include"),
        "-I" + str(recovery / "twrpminui"),
        "-I" + str(android / "system/libbase/include"), "-I" + str(android / "external/libpng"),
        "-I" + str(android / "external/libdrm"),
        "-I" + str(android / "external/libdrm/include/drm"),
        str(recovery / "twrpminui/graphics.cpp"),
        str(recovery / "twrpminui/graphics_utils.cpp"),
        str(recovery / "twrpminui/resources.cpp"),
        str(recovery / "twrpminui/tests/graphics_test.cpp"),
        "-l:libpng16.so.16", "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary), str(temporary / "frame.png")], check=True)

    (temporary / "cutils").mkdir()
    (temporary / "cutils/properties.h").write_text(
        '#pragma once\n#define PROPERTY_VALUE_MAX 92\n'
        'int property_get(const char*, char*, const char*);\n'
    )
    for test in ("input_transport_test", "touch_test"):
        binary = temporary / test
        subprocess.run([
            os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra",
            "-Wno-sign-compare", "-Wno-attributes", "-Wno-ignored-attributes",
            "-Wno-unused-function", "-Wno-unused-parameter",
            "-include", str(temporary / "host_compat.h"),
            "-I" + str(temporary), "-I" + str(recovery),
            "-I" + str(recovery / "twrpminui/include"),
            "-I" + str(recovery / "twrpinstall/include"),
            "-I" + str(android / "system/libbase/include"),
            str(recovery / "twrpminui/tests" / (test + ".cpp")),
            "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)

    # GC discards hardware methods; this target executes the real static DRM copy helpers.
    binary = temporary / "drm_damage_test"
    subprocess.run([
        os.environ.get("CXX", "g++"), "-std=c++17", "-ffunction-sections", "-fdata-sections",
        "-Wno-attributes", "-Wno-ignored-attributes", "-include", str(temporary / "host_compat.h"),
        "-I" + str(temporary), "-I" + str(recovery / "twrpminui/include"),
        "-I" + str(android / "system/libbase/include"),
        "-I" + str(android / "external/libdrm"),
        "-I" + str(android / "external/libdrm/include/drm"),
        "-I" + str(android / "external/libpng"),
        str(recovery / "twrpminui/tests/drm_damage_test.cpp"),
        str(recovery / "twrpminui/resources.cpp"),
        "-Wl,--gc-sections", "-l:libpng16.so.16", "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)

    # Syntax-check production DRM/fbdev backends and input/haptics sources.
    production_includes = [
        recovery, recovery / "twrpminui/include", recovery / "twrpinstall/include",
        android / "system/libbase/include", android / "system/core/libcutils/include",
        android / "system/liblog/include", android / "external/libdrm",
        android / "external/libdrm/include/drm",
    ]
    command = [os.environ.get("CXX", "g++"), "-std=c++20", "-fsyntax-only",
               "-Wno-attributes", "-Wno-ignored-attributes",
               "-include", str(temporary / "host_compat.h")]
    command += ["-I" + str(path) for path in production_includes]
    subprocess.run(command + [str(recovery / "twrpminui" / filename) for filename in (
        "graphics.cpp", "graphics_fbdev.cpp", "graphics_drm.cpp", "events.cpp",
        "twrp_input.cpp", "haptics.cpp", "force_feedback.cpp", "tspdrv.cpp",
    )], check=True)
    print("PASS: DRM/fbdev backends and input/haptics production syntax checks")
