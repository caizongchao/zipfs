local ninja = _G['ninja']
local path = _G['path']
local public = _G['public']
local files_in = _G['files_in']

ninja.toolchain = 'clangcl'

local debug = (function()
    for _, x in ipairs(arg) do
        if x == '--release' then
            return false
        end
    end
    return true
end)()

ninja.build_dir(debug and 'debug' or 'release')

local ATL_DIR = 'c:/apps/msvc/atlmfc/'

local ATL_INC_DIR = ATL_DIR .. 'include/'
local ATL_LIB_DIR = ATL_DIR .. 'lib/x64/'

local LIB_DIR = 'lib/'

local cc = ninja.target('cc')
    :type('phony')
    :define(public {
        debug and 'DEBUG' or 'NDEBUG', '_CRT_SECURE_NO_WARNINGS', '_CRT_NONSTDC_NO_WARNINGS', '_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS',
        '_WIN32', '_WIN32_WINNT=0x0601', 'NOMINMAX'
    })
    :c_flags(public { std = 'c11' })
    :cx_flags(public { '/arch:AVX', '/Z7', '/GS-', debug and '/Od' or '/O2' })
    :cx_flags(public { '-Wno-unused-value', '-Wno-microsoft-cast', '-Wno-int-to-pointer-cast', '-Wno-invalid-noreturn', '-Wno-microsoft-exception-spec', '-Wno-nontrivial-memcall', '-Wno-dangling-assignment-gsl' })
    :cxx_flags(public { std = 'c++latest', '/EHsc' })
    :ld_flags(public { '/DEBUG', '/OPT:REF' })
    :include_dir(public { 'include', 'C:/apps/msvc/atlmfc/include', 'w:/projects/mimalloc/deps/mimalloc/include', 'w:/projects/zip/zlib/include' })
    :lib_dir(public { 'lib' })
    :lib(public { 'mimalloc.lib', 'advapi32.lib', 'user32.lib', 'shell32.lib', 'zlib.lib' })

local zipfs = ninja.target('zipfs')
    :type('binary')
    :deps(cc)
    :cxx_pch('stdafx.h')
    :define('_ATL_NO_COM_SUPPORT'):include_dir(ATL_INC_DIR):lib_dir(ATL_LIB_DIR)
    :include_dir('dokan/include/dokan'):lib_dir('dokan/lib'):lib('dokan2.lib')
    :src('zipfs.cpp')

ninja.build()

-- ninja.watch(
--     '.', { '.', '*.cpp', '*.c', '*.h' }, function(fpath)
--         ninja.build(); print('=[' .. os.date("%X", os.time() + (8 * 60 * 60)) .. '] watching ==================')
--     end)
