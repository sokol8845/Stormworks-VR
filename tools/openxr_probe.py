"""Small Windows x64 capability probe; no session, frame loop or game injection.

ctypes ABI subset verified against Khronos OpenXR-SDK release-1.1.53:
https://github.com/KhronosGroup/OpenXR-SDK/blob/release-1.1.53/include/openxr/openxr.h
Keep this diagnostic separate from the future native adapter using the full SDK.
"""
import argparse
import ctypes as c
import json
import os
from pathlib import Path
import sys

U32, U64, I32, PTR = c.c_uint32, c.c_uint64, c.c_int32, c.c_void_p
HEADER = [('type', I32), ('next', PTR)]


class Extension(c.Structure):
    _fields_ = HEADER + [('name', c.c_char * 128), ('version', U32)]


class Application(c.Structure):
    _fields_ = [('name', c.c_char * 128), ('version', U32),
                ('engine', c.c_char * 128), ('engine_version', U32), ('api', U64)]


class CreateInfo(c.Structure):
    _fields_ = HEADER + [('flags', U64), ('application', Application),
                        ('layer_count', U32), ('layers', c.POINTER(c.c_char_p)),
                        ('extension_count', U32), ('extensions', c.POINTER(c.c_char_p))]


class InstanceProperties(c.Structure):
    _fields_ = HEADER + [('version', U64), ('name', c.c_char * 128)]


class SystemInfo(c.Structure):
    _fields_ = HEADER + [('form_factor', I32)]


class GraphicsProperties(c.Structure):
    _fields_ = [('max_height', U32), ('max_width', U32), ('max_layers', U32)]


class TrackingProperties(c.Structure):
    _fields_ = [('orientation', U32), ('position', U32)]


class SystemProperties(c.Structure):
    _fields_ = HEADER + [('system_id', U64), ('vendor', U32), ('name', c.c_char * 256),
                        ('graphics', GraphicsProperties), ('tracking', TrackingProperties)]


class HandProperties(c.Structure):
    _fields_ = HEADER + [('supported', U32)]


class ViewConfiguration(c.Structure):
    _fields_ = HEADER + [('width', U32), ('max_width', U32), ('height', U32),
                        ('max_height', U32), ('samples', U32), ('max_samples', U32)]


def verify_abi():
    if c.sizeof(PTR) != 8:
        raise RuntimeError('Use 64-bit Python for the 64-bit game/runtime')
    for structure, expected in [(Extension, 152), (Application, 272), (CreateInfo, 328),
                                (InstanceProperties, 152), (SystemInfo, 24),
                                (SystemProperties, 304), (HandProperties, 24),
                                (ViewConfiguration, 40)]:
        if c.sizeof(structure) != expected:
            raise RuntimeError(f'ABI mismatch: {structure.__name__}')
    if CreateInfo.application.offset != 24 or SystemProperties.graphics.offset != 284:
        raise RuntimeError('ABI field alignment mismatch')


def probe(loader_path: Path, report=None):
    verify_abi()
    loader_path = loader_path.resolve(strict=True)
    dll = c.WinDLL(str(loader_path), winmode=0x1100)  # DLL directory + system32
    get_proc = dll.xrGetInstanceProcAddr
    get_proc.argtypes = [PTR, c.c_char_p, c.POINTER(PTR)]
    get_proc.restype = I32
    instance = PTR()
    if report is None:
        report = {}
    report.update(loader=str(loader_path), status='initializing',
                  live_hand_tracking='not_tested_no_session')

    def function(name, *arguments):
        address = PTR()
        result = get_proc(instance, name.encode('ascii'), c.byref(address))
        if result < 0 or not address.value:
            raise RuntimeError(f'{name}: cannot resolve function ({result})')
        return c.WINFUNCTYPE(I32, *arguments)(address.value)

    def check(result, operation):
        if result < 0:
            raise RuntimeError(f'{operation}: XrResult {result}')

    enum = function('xrEnumerateInstanceExtensionProperties', c.c_char_p, U32,
                    c.POINTER(U32), c.POINTER(Extension))
    count = U32()
    check(enum(None, 0, c.byref(count), None), 'enumerate_extensions_count')
    extensions = (Extension * count.value)()
    for extension in extensions:
        extension.type = 2
    check(enum(None, count, c.byref(count), extensions), 'enumerate_extensions')
    names = sorted(e.name.decode() for e in extensions[:count.value])
    report['extensions'] = names
    hand_extension = 'XR_EXT_hand_tracking' in names
    report['hand_extension'] = hand_extension
    report['opengl_extension'] = 'XR_KHR_opengl_enable' in names
    enabled = (c.c_char_p * int(hand_extension))(*([b'XR_EXT_hand_tracking'] if hand_extension else []))
    info = CreateInfo(type=3, application=Application(name=b'StormworksVR probe',
                      version=1, engine=b'SWVR', api=1 << 48),
                      extension_count=len(enabled), extensions=enabled)
    create = function('xrCreateInstance', c.POINTER(CreateInfo), c.POINTER(PTR))
    check(create(c.byref(info), c.byref(instance)), 'create_instance')
    destroy = function('xrDestroyInstance', PTR)
    try:
        properties = InstanceProperties(type=32)
        check(function('xrGetInstanceProperties', PTR, c.POINTER(InstanceProperties))(
            instance, c.byref(properties)), 'instance_properties')
        report['runtime'] = properties.name.decode(errors='replace')
        v = properties.version
        report['runtime_version'] = f'{v >> 48}.{(v >> 32) & 65535}.{v & 0xffffffff}'
        system_id = U64()
        system_info = SystemInfo(type=4, form_factor=1)
        result = function('xrGetSystem', PTR, c.POINTER(SystemInfo), c.POINTER(U64))(
            instance, c.byref(system_info), c.byref(system_id))
        report['get_system_result'] = result
        if result == -35:
            report['status'] = 'headset_unavailable'
            return report
        check(result, 'get_system')
        hands = HandProperties(type=1000051000)
        system = SystemProperties(type=5, next=c.addressof(hands) if hand_extension else None)
        check(function('xrGetSystemProperties', PTR, U64, c.POINTER(SystemProperties))(
            instance, system_id, c.byref(system)), 'system_properties')
        report['system'] = system.name.decode(errors='replace')
        report['supports_hand_tracking'] = bool(hands.supported) if hand_extension else False
        report['orientation_tracking'] = bool(system.tracking.orientation)
        report['position_tracking'] = bool(system.tracking.position)
        views_count = U32()
        enum_views = function('xrEnumerateViewConfigurationViews', PTR, U64, I32, U32,
                              c.POINTER(U32), c.POINTER(ViewConfiguration))
        check(enum_views(instance, system_id, 2, 0, c.byref(views_count), None), 'stereo_views_count')
        views = (ViewConfiguration * views_count.value)()
        for view in views:
            view.type = 41
        check(enum_views(instance, system_id, 2, views_count, c.byref(views_count), views), 'stereo_views')
        report['stereo_views'] = [{'width': v.width, 'height': v.height,
                                   'samples': v.samples} for v in views[:views_count.value]]
        report['status'] = 'capabilities_only'
        return report
    finally:
        report['destroy_instance_result'] = destroy(instance)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--loader', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    # Valve's process-local log override; keep runtime logs in the project.
    # https://github.com/ValveSoftware/openvr/blob/master/src/vrcore/vrpathregistry_public.cpp
    log_dir = args.output.resolve().parent / 'steamvr-logs'
    log_dir.mkdir(parents=True, exist_ok=True)
    os.environ['VR_LOG_PATH'] = str(log_dir)
    result = {}
    try:
        probe(args.loader, result)
    except (OSError, RuntimeError, AttributeError) as error:
        result.update(status='error', error=str(error))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))
    sys.exit(1 if result['status'] == 'error' else 0)
