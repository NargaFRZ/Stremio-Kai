"""Copy Kai's original PE resources onto the source-built native executable.

LoadLibraryEx uses data-only flags: the source executable is never executed.
Every resource is compared after writing, and non-resource sections must retain
their bytes. Python and this script are build dependencies, not runtime files.
"""
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
from pathlib import Path
import subprocess
import sys


k = C.WinDLL('kernel32', use_last_error=True)
PTR = C.c_void_p
TYPE_CALLBACK = C.WINFUNCTYPE(W.BOOL, W.HMODULE, PTR, PTR)
NAME_CALLBACK = C.WINFUNCTYPE(W.BOOL, W.HMODULE, PTR, PTR, PTR)
LANG_CALLBACK = C.WINFUNCTYPE(W.BOOL, W.HMODULE, PTR, PTR, W.WORD, PTR)


def api(name, result, *arguments):
    function = getattr(k, name)
    function.restype = result
    function.argtypes = arguments
    return function


load = api('LoadLibraryExW', W.HMODULE, W.LPCWSTR, W.HANDLE, W.DWORD)
free = api('FreeLibrary', W.BOOL, W.HMODULE)
enum_types = api('EnumResourceTypesW', W.BOOL, W.HMODULE, TYPE_CALLBACK, PTR)
enum_names = api('EnumResourceNamesW', W.BOOL, W.HMODULE, PTR, NAME_CALLBACK, PTR)
enum_languages = api('EnumResourceLanguagesW', W.BOOL, W.HMODULE, PTR, PTR, LANG_CALLBACK, PTR)
find = api('FindResourceExW', W.HANDLE, W.HMODULE, PTR, PTR, W.WORD)
size_of = api('SizeofResource', W.DWORD, W.HMODULE, W.HANDLE)
load_resource = api('LoadResource', W.HANDLE, W.HMODULE, W.HANDLE)
lock = api('LockResource', PTR, W.HANDLE)
begin_update = api('BeginUpdateResourceW', W.HANDLE, W.LPCWSTR, W.BOOL)
update = api('UpdateResourceW', W.BOOL, W.HANDLE, PTR, PTR, W.WORD, PTR, W.DWORD)
end_update = api('EndUpdateResourceW', W.BOOL, W.HANDLE, W.BOOL)


def checked(value):
    if not value:
        raise C.WinError(C.get_last_error())
    return value


def resource_id(pointer):
    return (pointer or 0) if (pointer or 0) <= 0xffff else C.wstring_at(pointer)


def resource_pointer(value):
    return PTR(value) if isinstance(value, int) else C.cast(C.c_wchar_p(value), PTR)


def resources(path):
    module = checked(load(str(path), None, 0x2 | 0x20))
    values, errors = {}, []

    @LANG_CALLBACK
    def language_callback(module, kind, name, language, _):
        try:
            info = checked(find(module, kind, name, language))
            length = size_of(module, info)
            pointer = checked(lock(checked(load_resource(module, info))))
            values[(resource_id(kind), resource_id(name), language)] = C.string_at(pointer, length)
            return True
        except Exception as error:
            errors.append(error)
            return False

    @NAME_CALLBACK
    def name_callback(module, kind, name, _):
        return enum_languages(module, kind, name, language_callback, None)

    @TYPE_CALLBACK
    def type_callback(module, kind, _):
        return enum_names(module, kind, name_callback, None)

    try:
        success = enum_types(module, type_callback, None)
        if errors:
            raise errors[0]
        checked(success)
        if not values:
            raise RuntimeError('No resources were found in the executable')
        return values
    finally:
        free(module)


def sections(path):
    report = json.loads(subprocess.check_output([
        sys.executable, str(Path(__file__).with_name('inspect-native.py')), str(path)
    ], text=True))[0]
    return {item['name']: item['sha256'] for item in report['sections'] if item['name'] != '.rsrc'}


def preserve(source, destination):
    source, destination = Path(source).resolve(), Path(destination).resolve()
    original = resources(source)
    before = sections(destination)
    session = checked(begin_update(str(destination), True))
    try:
        for (kind, name, language), data in original.items():
            if not data:
                raise RuntimeError('Unexpected empty resource')
            buffer = C.create_string_buffer(data)
            checked(update(session, resource_pointer(kind), resource_pointer(name),
                           language, C.cast(buffer, PTR), len(data)))
        checked(end_update(session, False))
        session = None
    finally:
        if session:
            end_update(session, True)
    if resources(destination) != original:
        raise RuntimeError('Resource preservation verification failed')
    if sections(destination) != before:
        raise RuntimeError('Resource update changed a non-resource section')
    report = [{'type': kind, 'name': name, 'language': language, 'bytes': len(data),
               'sha256': hashlib.sha256(data).hexdigest()}
              for (kind, name, language), data in original.items()]
    print(json.dumps({'preserved_resources': report}, indent=2))


if __name__ == '__main__':
    preserve(*sys.argv[1:])
