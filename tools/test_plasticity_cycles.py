# SPDX-License-Identifier: Apache-2.0

import ctypes
import math
import os
import sys
import tempfile
from pathlib import Path


def smoke_test(renderer):
    void = ctypes.c_void_p
    int32 = ctypes.c_int32
    uint32 = ctypes.c_uint32
    float32 = ctypes.c_float
    double = ctypes.c_double
    size = ctypes.c_size_t
    pointer = ctypes.POINTER

    def status_call(name, *types):
        call = getattr(renderer, "plasticity_cycles_" + name)
        call.argtypes = types
        call.restype = ctypes.c_char_p

        def check(*args):
            result = call(*args)
            if result is not None:
                raise RuntimeError(result.decode())

        return check

    create = status_call("create", pointer(void))
    destroy = renderer.plasticity_cycles_destroy
    destroy.argtypes = [void]
    destroy.restype = None
    add_materials = status_call(
        "add_materials", void, size, pointer(double), pointer(double), pointer(double), pointer(int32)
    )
    add_meshes = status_call(
        "add_meshes",
        void,
        size,
        pointer(float32),
        pointer(float32),
        pointer(uint32),
        pointer(int32),
        pointer(int32),
        pointer(int32),
    )
    add_lights = status_call(
        "add_lights", void, size, pointer(int32), pointer(double), pointer(double), pointer(double)
    )
    set_camera = status_call("set_camera_matrices", void, pointer(double), pointer(double))
    set_environment = status_call("set_environment", void, ctypes.c_char_p, double)
    render = status_call("render", void, int32, int32, int32, pointer(float32))

    scene = void()
    create(ctypes.byref(scene))
    assert scene.value
    try:
        material = int32()
        add_materials(scene, 1, (double * 3)(0.6, 0.6, 0.6), (double * 1)(0), (double * 1)(0.5), ctypes.byref(material))
        add_meshes(
            scene,
            1,
            (float32 * 12)(-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0),
            (float32 * 12)(*([0, 0, 1] * 4)),
            (uint32 * 6)(0, 1, 2, 0, 2, 3),
            (int32 * 2)(0, 12),
            (int32 * 2)(0, 6),
            (int32 * 2)(material.value, material.value),
        )
        add_lights(
            scene, 2, (int32 * 2)(1, 2), (double * 6)(0, 0, 2, 1, 0, 3),
            (double * 2)(0.1, 1), (double * 2)(100, 40),
        )
        width, height = 9, 7
        f = 1 / math.tan(0.5)
        near, far = 0.1, 100
        view = (double * 16)(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 4, 1)
        projection = (double * 16)(
            f * height / width, 0, 0, 0,
            0, f, 0, 0,
            0, 0, -(far + near) / (far - near), -1,
            0, 0, -2 * far * near / (far - near), 0,
        )
        set_camera(scene, view, projection)
        pixels = (float32 * (width * height * 4))()
        center = 4 * ((height // 2) * width + width // 2)
        render(scene, width, height, 4, pixels)
        assert math.isfinite(pixels[center]) and pixels[center] > 0, pixels[center : center + 4]
        assert abs(pixels[center + 3] - 1) < 1e-5, pixels[center : center + 4]
        assert abs(pixels[0]) < 1e-5, pixels[:4]

        with tempfile.TemporaryDirectory() as temp:
            hdr = Path(temp) / "studio.hdr"
            hdr.write_bytes(b"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 4\n" + bytes((128, 128, 128, 128)) * 8)
            set_environment(scene, str(hdr).encode("utf-8"), 0.3)
            orthographic = (double * 16)(
                0.5, 0, 0, 0,
                0, 2 / 3, 0, 0,
                0, 0, -2 / (far - near), 0,
                0, 0, -(far + near) / (far - near), 1,
            )
            set_camera(scene, view, orthographic)
            render(scene, width, height, 4, pixels)
        assert math.isfinite(pixels[0]) and pixels[0] > 0, pixels[:4]
    finally:
        destroy(scene)


library = Path(sys.argv[1]).resolve()
if os.name == "nt":
    with os.add_dll_directory(str(library.parent)):
        smoke_test(ctypes.CDLL(str(library)))
else:
    smoke_test(ctypes.CDLL(str(library)))
