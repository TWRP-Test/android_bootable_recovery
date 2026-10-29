#!/usr/bin/env python3
"""Run framebuffer/PNG regression checks without an Android build or device.

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
    (temporary / "cutils").mkdir()
    (temporary / "cutils/properties.h").write_text(
        '#pragma once\n#define PROPERTY_VALUE_MAX 92\n'
        'int property_get(const char*, char*, const char*);\n'
    )
    binary = temporary / "graphics_test"
    subprocess.run([
        os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-DOVERSCAN_PERCENT=0", "-DTW_ROTATION=0",
        "-I" + str(temporary), "-I" + str(recovery / "twrpminui/include"),
        "-I" + str(recovery / "twrpminui"), "-I" + str(android / "external/libpng"),
        str(recovery / "twrpminui/graphics.cpp"),
        str(recovery / "twrpminui/graphics_utils.cpp"),
        str(recovery / "twrpminui/tests/graphics_test.cpp"),
        "-l:libpng16.so.16", "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary), str(temporary / "frame.png")], check=True)
