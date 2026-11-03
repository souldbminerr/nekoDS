# Switch toolchain for nekoDS SDL3/dlink frontend.
# Based on melonDS-new cmake/Toolchain-cross-Switch.cmake, extended with
# include/lib paths for SDL-dlink (headers at build time) and libnx-dlink
# (host link). Expects sibling checkouts by default:
#
#   <root>/nekoDS      this repo
#   <root>/SDL-dlink    SDL3 fork (provides include/SDL3 + dist dnro)
#   <root>/neko3d       neko3d (provides include/deko3d.h for SDL headers)
#
# Override with -DSDL_DLINK_DIR=... -DNEKO3D_DIR=... -DDLINK_DIR=...

if(DEFINED ENV{DEVKITPRO})
    set(DEVKITPRO $ENV{DEVKITPRO})
else()
    message(FATAL_ERROR "Could not find DEVKITPRO in environment")
endif()

set(CMAKE_SYSTEM_NAME Generic CACHE INTERNAL "system name")

# sibling defaults (user layout: D:/l4t/nekoDS + D:/l4t/SDL-dlink + ...)
get_filename_component(_SWITCH_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(SDL_DLINK_DIR "${_SWITCH_ROOT}/../SDL-dlink" CACHE PATH "SDL-dlink checkout")
set(NEKO3D_DIR "${_SWITCH_ROOT}/../neko3d" CACHE PATH "neko3d checkout")
set(DLINK_DIR "${NEKO3D_DIR}/extern/libnx-dlink" CACHE PATH "libnx-dlink checkout")

if(DEFINED ENV{LIBNX})
    set(LIBNX $ENV{LIBNX})
else()
    set(LIBNX ${DEVKITPRO}/libnx)
endif()

function(create_devkit DEVKIT)
    if(DEFINED ENV{DEVKIT${DEVKIT}})
        set(DEVKIT${DEVKIT} $ENV{DEVKIT${DEVKIT}} PARENT_SCOPE)
    else()
        set(DEVKIT${DEVKIT} ${DEVKITPRO}/devkit${DEVKIT} PARENT_SCOPE)
    endif()

    set(CMAKE_PROGRAM_PATH ${DEVKIT${DEVKIT}}/bin CACHE INTERNAL "program path")

    set(extension)
    if(CMAKE_HOST_WIN32)
        set(extension .exe)
    endif()

    find_program(CMAKE_AR ${cross_prefix}gcc-ar${extension})
    find_program(CMAKE_RANLIB ${cross_prefix}gcc-ranlib${extension})
    find_program(CMAKE_C_COMPILER ${cross_prefix}gcc${extension})
    find_program(CMAKE_CXX_COMPILER ${cross_prefix}g++${extension})
    find_program(CMAKE_ASM_COMPILER ${cross_prefix}gcc${extension})
    find_program(CMAKE_LINKER ${cross_prefix}ld${extension})
    set(CMAKE_C_FLAGS ${inc_flags} CACHE INTERNAL "c compiler flags")
    set(CMAKE_ASM_FLAGS ${inc_flags} CACHE INTERNAL "assembler flags")
    set(CMAKE_CXX_FLAGS ${inc_flags} CACHE INTERNAL "cxx compiler flags")

    set(CMAKE_EXE_LINKER_FLAGS ${link_flags} CACHE INTERNAL "exe link flags")
    set(CMAKE_MODULE_LINKER_FLAGS ${link_flags} CACHE INTERNAL "module link flags")
    set(CMAKE_SHARED_LINKER_FLAGS ${link_flags} CACHE INTERNAL "shared link flags")

    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER CACHE INTERNAL "")
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY CACHE INTERNAL "")
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY CACHE INTERNAL "")
    set(PKG_CONFIG_EXECUTABLE "/dev/null" CACHE INTERNAL "" FORCE)
endfunction()

set(cross_prefix aarch64-none-elf-)
# NOTE: -ftls-model=local-exec is mandatory here. Without it GCC emits
# global-dynamic/emutls TLS access (bl __aarch64_read_tp + [x0,x0] store)
# for `thread_local NDS* NDS::Current`, which faults on Horizon. neko3d
# builds with the same flag for the same reason.
set(arch_flags "-mtune=cortex-a57 -ffunction-sections -march=armv8-a+crc+crypto -mtp=soft -fPIE -ftls-model=local-exec")
set(inc_flags "-I${LIBNX}/include -I${SDL_DLINK_DIR}/include -I${NEKO3D_DIR}/include -I${DLINK_DIR}/include ${arch_flags}")
set(link_flags "-L${LIBNX}/lib -L${DLINK_DIR}/lib -specs=${LIBNX}/switch.specs ${arch_flags}")

set(CMAKE_SYSTEM_PROCESSOR aarch64 CACHE INTERNAL "processor")
set(CMAKE_LIBRARY_ARCHITECTURE aarch64-none-elf CACHE INTERNAL "abi")

set(SWITCH ON)
add_definitions(-D__SWITCH__)

create_devkit(A64)

set(CMAKE_FIND_ROOT_PATH ${DEVKITA64}/${CMAKE_LIBRARY_ARCHITECTURE} ${DEVKITPRO}/portlibs/switch)

find_program(ELF2NRO elf2nro)
find_program(NACPTOOL nacptool)
find_program(UAM uam)

# Offline ("static") UAM shader compilation. Shaders are GLSL in the repo,
# compiled at build time into .dksh blobs in romfs — no runtime UAM needed,
# which keeps the dynamic neko3d/sdl3 loading model intact.
function(compile_shader stage relativepath)
	get_filename_component(filename ${relativepath} NAME_WE)
	list(APPEND shader_paths ${CMAKE_BINARY_DIR}/romfs/shaders/${filename}.dksh)
	set(shader_paths ${shader_paths} PARENT_SCOPE)
	add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/romfs/shaders/${filename}.dksh
        COMMAND ${UAM} --stage=${stage} --out ${CMAKE_BINARY_DIR}/romfs/shaders/${filename}.dksh ${CMAKE_CURRENT_SOURCE_DIR}/${relativepath}.glsl
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${relativepath}.glsl)
endfunction()

function(compile_shader_instance stage relativepath newname defines)
	file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/generated_shaders)

	foreach(var IN ITEMS ${defines})
		set(${var} 1)
	endforeach()

	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/${relativepath}.glsl ${CMAKE_BINARY_DIR}/generated_shaders/${newname}.glsl)
	list(APPEND shader_paths ${CMAKE_BINARY_DIR}/romfs/shaders/${newname}.dksh)
	set(shader_paths ${shader_paths} PARENT_SCOPE)
	add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/romfs/shaders/${newname}.dksh
        COMMAND ${UAM} --stage=${stage} --out ${CMAKE_BINARY_DIR}/romfs/shaders/${newname}.dksh ${CMAKE_BINARY_DIR}/generated_shaders/${newname}.glsl
        DEPENDS ${CMAKE_BINARY_DIR}/generated_shaders/${newname}.glsl)
endfunction()
