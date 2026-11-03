# nekoDS Switch/SDL3 frontend (dynamic sdl3.dnro + neko3d.dnro).
# Built only with the Switch toolchain:
#   cmake -B build/switch --toolchain cmake/Toolchain-Switch-DLink.cmake \
#     -DCMAKE_BUILD_TYPE=Release \
#     -DBUILD_QT_SDL=OFF -DBUILD_SWITCH_SDL=ON
#
# Binary model (mirrors SDL-dlink smoke + qlaunch-ext loader):
#   - host NRO links core + nx + dlink, -Wl,--export-dynamic
#   - every sdl3.dnro / neko3d.dnro import is forced into the host link
#     via -Wl,-u,<sym> generated from their .undef files, so
#     dlink_register_host()/host_provide_all() can expose it at runtime
#   - at launch the host dlopens neko3d (GLOBAL) then sdl3 (GLOBAL) from
#     sdmc:/lib/ and binds both the SDL API and the dk* dispatch table
#
# GPU: ported melonDS-new deko3d renderers (GPU2D_Deko/GPU3D_Deko) driven
# through the dynamic dispatch table; shaders are compiled offline ("static
# UAM") at build time into romfs .dksh blobs. Software renderer fallback
# stays available at runtime if deko init fails.

if (NOT UAM)
    message(FATAL_ERROR "Switch/SDL: uam shader compiler not found (needs dekotools). Cannot build deko GPU shaders.")
endif()

set(SOURCES_SWITCH_SDL
    main.cpp
    Platform.cpp
    PlatformConfig.cpp
    ui.cpp
    SwitchSDL.h
    SwitchLog.h
    DekoDispatch.h
    DekoDispatch.cpp
    DekoShim.cpp
    DekoAPI.h
    DekoContext.h
    DekoContext.cpp
    DekoGPU.h
    DekoGPU.cpp
    GpuMemHeap.h
    GpuMemHeap.cpp
    UploadBuffer.h
    UploadBuffer.cpp
    CmdMemRing.h
    GPU2D_Deko.h
    GPU2D_Deko.cpp
    GPU3D_Deko.h
    GPU3D_Deko.cpp
)

# Regeneration helpers (not built): gen_dispatch.py, port_deko.py.

add_executable(nekoDS-switch.elf ${SOURCES_SWITCH_SDL})

target_include_directories(nekoDS-switch.elf PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../.."
    "${SDL_DLINK_DIR}/include"
    "${NEKO3D_DIR}/include"
    "${DLINK_DIR}/include"
)

target_link_libraries(nekoDS-switch.elf PRIVATE core)
target_link_libraries(nekoDS-switch.elf PRIVATE dlink nx m)

# Host must export its dynamic symbols for dlink modules.
target_link_options(nekoDS-switch.elf PRIVATE
    "LINKER:--export-dynamic"
    "LINKER:-z,nopack-relative-relocs"
    "LINKER:--gc-sections"
    # Symbols only the DEBUG neko3d.dnro imports (applet error dialog).
    # Forced in so dlink_register_host() can expose them; harmless otherwise.
    "LINKER:-u,errorApplicationCreate"
    "LINKER:-u,errorApplicationShow"
)

# --- hostneed flags: force every dnro import into the host link ---
# sdl3.dnro + neko3d.dnro publish their host imports in .undef files.
set(_sdl_undef "${SDL_DLINK_DIR}/dnro/sdl3.undef")
set(_neko_undef "${NEKO3D_DIR}/neko3d-module/neko3d.undef")
set(_hostneed "")
foreach(_f ${_sdl_undef} ${_neko_undef})
    if (EXISTS "${_f}")
        file(STRINGS "${_f}" _syms)
        foreach(_s ${_syms})
            string(STRIP "${_s}" _s)
            if (_s AND NOT _s MATCHES "^#")
                list(APPEND _hostneed "LINKER:-u,${_s}")
            endif()
        endforeach()
    else()
        message(WARNING "Switch/SDL: ${_f} not found; run SDL-dlink build.sh / neko3d-module make first")
    endif()
endforeach()
list(REMOVE_DUPLICATES _hostneed)
target_link_options(nekoDS-switch.elf PRIVATE ${_hostneed})

# --- offline UAM shaders -> romfs (donor: melonDS-new frontend/switch) ---
make_directory(${CMAKE_BINARY_DIR}/romfs/shaders)

compile_shader(vert shaders/FullscreenQuad_vsh)

compile_shader_instance(frag shaders/ComposeBGOBJ_fsh ComposeBGOBJ_fsh "ComposeBGOBJ")
compile_shader_instance(frag shaders/ComposeBGOBJ_fsh ComposeBGOBJDirectBitmapOnly_fsh "ShowBitmap")
compile_shader_instance(frag shaders/ComposeBGOBJ_fsh ComposeBGOBJShowBitmap_fsh "ShowBitmap;ComposeBGOBJ")
compile_shader_instance(frag shaders/BGText_fsh BGText4bpp_fsh "Text4bpp")
compile_shader_instance(frag shaders/BGText_fsh BGText4bppMosaic_fsh "Text4bpp;Mosaic")
compile_shader_instance(frag shaders/BGText_fsh BGText8bpp_fsh "Text8bpp")
compile_shader_instance(frag shaders/BGText_fsh BGText8bppMosaic_fsh "Text8bpp;Mosaic")
compile_shader_instance(frag shaders/BGAffine_fsh BGAffine_fsh "Affine")
compile_shader_instance(frag shaders/BGAffine_fsh BGAffineMosaic_fsh "Affine;Mosaic")
compile_shader_instance(frag shaders/BGAffine_fsh BGExtendedBitmap8bpp_fsh "ExtendedBitmap8bpp")
compile_shader_instance(frag shaders/BGAffine_fsh BGExtendedBitmap8bppMosaic_fsh "ExtendedBitmap8bpp;Mosaic")
compile_shader_instance(frag shaders/BGAffine_fsh BGExtendedBitmapDirect_fsh "ExtendedBitmapDirectColor")
compile_shader_instance(frag shaders/BGAffine_fsh BGExtendedBitmapDirectMosaic_fsh "ExtendedBitmapDirectColor;Mosaic")
compile_shader_instance(frag shaders/BGAffine_fsh BGExtendedMixed_fsh "ExtendedMixed")
compile_shader_instance(frag shaders/BGAffine_fsh BGExtendedMixedMosaic_fsh "ExtendedMixed;Mosaic")
compile_shader_instance(vert shaders/OBJ_vsh OBJRegular_vsh "OBJRegular")
compile_shader_instance(vert shaders/OBJ_vsh OBJAffine_vsh "OBJAffine")
compile_shader_instance(frag shaders/OBJ_fsh OBJ4bpp_fsh "OBJ4bpp")
compile_shader_instance(frag shaders/OBJ_fsh OBJ8bpp_fsh "OBJ8bpp")
compile_shader_instance(frag shaders/OBJ_fsh OBJBitmap_fsh "OBJBitmap")
compile_shader_instance(frag shaders/OBJ_fsh OBJWindow4bpp_fsh "OBJ4bpp;OBJWindow")
compile_shader_instance(frag shaders/OBJ_fsh OBJWindow8bpp_fsh "OBJ8bpp;OBJWindow")
compile_shader_instance(comp shaders/GPU3D_Comp InterpXSpansZBuffer "InterpSpans;ZBuffer")
compile_shader_instance(comp shaders/GPU3D_Comp InterpXSpansWBuffer "InterpSpans;WBuffer")
compile_shader_instance(comp shaders/GPU3D_Comp BinCombined "BinCombined")
compile_shader_instance(comp shaders/GPU3D_Comp DepthBlendWBuffer "DepthBlend;WBuffer")
compile_shader_instance(comp shaders/GPU3D_Comp DepthBlendZBuffer "DepthBlend;ZBuffer")
compile_shader_instance(comp shaders/GPU3D_Comp RasteriseNoTextureWBuffer "Rasterise;WBuffer;NoTexture")
compile_shader_instance(comp shaders/GPU3D_Comp RasteriseNoTextureWBufferToon "Rasterise;WBuffer;NoTexture;Toon")
compile_shader_instance(comp shaders/GPU3D_Comp RasteriseNoTextureWBufferHighlight "Rasterise;WBuffer;NoTexture;Highlight")
compile_shader_instance(comp shaders/GPU3D_Comp RasteriseUseTextureDecalWBuffer "Rasterise;WBuffer;UseTexture;Decal")
compile_shader_instance(comp shaders/GPU3D_Comp RasteriseUseTextureModulateWBuffer "Rasterise;WBuffer;UseTexture;Modulate")
compile_shader_instance(comp shaders/GPU3D_Comp RasteriseUseTextureToonWBuffer "Rasterise;WBuffer;UseTexture;Toon")
compile_shader_instance(comp shaders/GPU3D_Comp RasteriseUseTextureHighlightWBuffer "Rasterise;WBuffer;UseTexture;Highlight")
compile_shader_instance(comp shaders/GPU3D_Comp RasteriseShadowMaskWBuffer "Rasterise;WBuffer;ShadowMask")
compile_shader_instance(comp shaders/GPU3D_Comp RasteriseNoTextureZBuffer "Rasterise;ZBuffer;NoTexture")
compile_shader_instance(comp shaders/GPU3D_Comp RasteriseNoTextureZBufferToon "Rasterise;ZBuffer;NoTexture;Toon")
compile_shader_instance(comp shaders/GPU3D_Comp RasteriseNoTextureZBufferHighlight "Rasterise;ZBuffer;NoTexture;Highlight")
compile_shader_instance(comp shaders/GPU3D_Comp RasteriseUseTextureDecalZBuffer "Rasterise;ZBuffer;UseTexture;Decal")
compile_shader_instance(comp shaders/GPU3D_Comp RasteriseUseTextureModulateZBuffer "Rasterise;ZBuffer;UseTexture;Modulate")
compile_shader_instance(comp shaders/GPU3D_Comp RasteriseUseTextureToonZBuffer "Rasterise;ZBuffer;UseTexture;Toon")
compile_shader_instance(comp shaders/GPU3D_Comp RasteriseUseTextureHighlightZBuffer "Rasterise;ZBuffer;UseTexture;Highlight")
compile_shader_instance(comp shaders/GPU3D_Comp RasteriseShadowMaskZBuffer "Rasterise;ZBuffer;ShadowMask")
compile_shader_instance(comp shaders/GPU3D_Comp FinalPass "FinalPass")
compile_shader_instance(comp shaders/GPU3D_Comp FinalPassEdge "FinalPass;EdgeMarking")
compile_shader_instance(comp shaders/GPU3D_Comp FinalPassFog "FinalPass;Fog")
compile_shader_instance(comp shaders/GPU3D_Comp FinalPassEdgeFog "FinalPass;EdgeMarking;Fog")
compile_shader_instance(comp shaders/GPU3D_Comp FinalPassAA "FinalPass;AntiAliasing")
compile_shader_instance(comp shaders/GPU3D_Comp FinalPassEdgeAA "FinalPass;EdgeMarking;AntiAliasing")
compile_shader_instance(comp shaders/GPU3D_Comp FinalPassFogAA "FinalPass;Fog;AntiAliasing")
compile_shader_instance(comp shaders/GPU3D_Comp FinalPassEdgeFogAA "FinalPass;EdgeMarking;Fog;AntiAliasing")
compile_shader_instance(comp shaders/GPU3D_Comp ClearCoarseBinMask "ClearCoarseBinMask")
compile_shader_instance(comp shaders/GPU3D_Comp ClearIndirectWorkCount "ClearIndirectWorkCount")
compile_shader_instance(comp shaders/GPU3D_Comp CalculateWorkOffsets "CalculateWorkOffsets")
compile_shader_instance(comp shaders/GPU3D_Comp SortWork "SortWork")

add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/nekoDS-switch.nacp"
    COMMAND ${NACPTOOL} --create "nekoDS" "Horizon-OC" "1.1" "${CMAKE_BINARY_DIR}/nekoDS-switch.nacp"
    COMMENT "Generating Switch NACP"
)

# NRO packaging. NOTE: romfsdir embeds romfs/shaders/*.dksh (offline UAM).
# sdmc:/lib/{sdl3,neko3d}.dnro still ship alongside (dynamic loading).
add_custom_target(nekoDS-switch.nro ALL
    COMMAND ${ELF2NRO} "$<TARGET_FILE:nekoDS-switch.elf>" "${CMAKE_BINARY_DIR}/nekoDS-switch.nro"
            --nacp="${CMAKE_BINARY_DIR}/nekoDS-switch.nacp" --romfsdir="${CMAKE_BINARY_DIR}/romfs"
    DEPENDS nekoDS-switch.elf
    "${CMAKE_BINARY_DIR}/nekoDS-switch.nacp"
    "${shader_paths}"
    COMMENT "Packaging Switch NRO (+ romfs shaders)"
)
