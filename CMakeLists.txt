set(CMAKE_BUILD_TYPE "Release")

cmake_minimum_required(VERSION 3.21)
project(ybot-capsule-box3d-sdl3)

# Fix for Windows path length limits with external libraries
set(CMAKE_OBJECT_PATH_MAX 512)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (EMSCRIPTEN)
    set(SDL3_DIR "C:/libs/SDL3-devel-3.4.12-wasm/lib/cmake/SDL3")
    set(SDL3_image_DIR "C:/libs/SDL3_image-devel-3.5.0-wasm/lib/cmake/SDL3_image")
    set(SDL3_ttf_DIR "C:/libs/SDL3_ttf-devel-3.3.0-wasm/lib/cmake/SDL3_ttf")
    set(box3d_DIR "C:/libs/box3d-0.1.0-wasm/lib/cmake/box3d")
elseif (WIN32 AND NOT ANDROID)
    set(SDL3_DIR "C:/libs/SDL3-devel-3.4.12-mingw/lib/cmake/SDL3")
    set(SDL3_ttf_DIR "C:/libs/SDL3_ttf-devel-3.2.2-mingw/lib/cmake/SDL3_ttf")
    set(SDL3_image_DIR "C:/libs/SDL3_image-devel-3.5.0-mingw/lib/cmake/SDL3_image")
    set(box3d_DIR "C:/libs/box3d-0.1.0-mingw/lib/cmake/box3d")
endif()

if(ANDROID)
    set(box3d_DIR "C:/libs/box3d-0.1.0-android/arm64-v8a/lib/cmake/box3d")

    # Native library target for Android (.so)
    add_library(app SHARED)
else()
    # Executable for Desktop / Web
    add_executable(app)
endif()

find_package(SDL3 REQUIRED CONFIG)
find_package(SDL3_ttf REQUIRED CONFIG)
find_package(SDL3_image REQUIRED CONFIG)
find_package(box3d REQUIRED CONFIG)

target_include_directories(app PRIVATE
    C:/libs/imgui-1.92.9-mingw/include
    C:/libs/cglm-0.9.6/include
    C:/libs/cgltf-1.15
    C:/libs/ozz-animation-0.16.0-mingw/include
)

if (EMSCRIPTEN)
    target_link_options(app PRIVATE 
        "-Oz"                   # Maximize code size optimization
        "-sMALLOC=emmalloc"     # Use minimal memory allocator
        "-sUSE_SDL=0"           # Disable built-in SDL
        "-sMAX_WEBGL_VERSION=2" # Force WebGL 2.0 support
        "-sMIN_WEBGL_VERSION=2" # Do not fallback to WebGL 1.0
        "-sFULL_ES3=1"          # Enable GLES3 -> WebGL 2 mapping
        "SHELL:--embed-file ${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets"
    )
endif()

if (WIN32)
    target_include_directories(app PRIVATE C:/libs/glad-0.1.36/opengl-3.3/include)

    target_sources(app
        PRIVATE
        C:/libs/glad-0.1.36/opengl-3.3/src/glad.c
    )

    # Copy the assets folder to the dist folder (clearing it first)
    if(EXISTS "${CMAKE_SOURCE_DIR}/assets")
        add_custom_command(TARGET app POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:app>/assets"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_SOURCE_DIR}/assets"
                    "$<TARGET_FILE_DIR:app>/assets"
            COMMENT "Copying assets directory"
        )
    endif()

    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(app PRIVATE -mconsole)
    else()
        target_link_options(app PRIVATE -mwindows)
    endif()
endif()

if(ANDROID)
    target_link_libraries(app PRIVATE EGL GLESv3)

    # Define the target Android assets directory
    set(ANDROID_ASSETS_DIR "${CMAKE_SOURCE_DIR}/android-project/app/src/main/assets")

    # Copy the assets folder to the Android project structure (clearing it first)
    if(EXISTS "${CMAKE_SOURCE_DIR}/assets")
        add_custom_command(TARGET app POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E remove_directory "${ANDROID_ASSETS_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_SOURCE_DIR}/assets"
                    "${ANDROID_ASSETS_DIR}"
            COMMENT "Copying assets to Android project structure"
        )
    endif()
endif()

target_sources(app
PRIVATE
    C:/libs/imgui-1.92.9/imgui.cpp
    C:/libs/imgui-1.92.9/imgui_draw.cpp
    C:/libs/imgui-1.92.9/imgui_tables.cpp
    C:/libs/imgui-1.92.9/imgui_widgets.cpp
    C:/libs/imgui-1.92.9/backends/imgui_impl_opengl3.cpp
    C:/libs/imgui-1.92.9/backends/imgui_impl_sdl3.cpp
    src/app.h
    src/file_utils.h
    src/file_utils.c
    src/main.cpp
    src/ozz_sdl_stream.h
    src/physics_debug.h
    src/physics_debug.cpp
    src/projection_utils.h
    src/shader_program.h
    src/shader_program.c
    src/texture_utils.h
)

if (WIN32 AND NOT ANDROID AND NOT EMSCRIPTEN)
    target_link_options(app PRIVATE -static)

    target_link_libraries(app PRIVATE
        SDL3_image::SDL3_image-static
        SDL3_ttf::SDL3_ttf-static
        SDL3::SDL3-static
        # Ozz-animation static libraries (order matters for GCC/MinGW linkers)
        C:/libs/ozz-animation-0.16.0-mingw/lib/libozz_animation_offline_r.a
        C:/libs/ozz-animation-0.16.0-mingw/lib/libozz_animation_r.a
        C:/libs/ozz-animation-0.16.0-mingw/lib/libozz_geometry_r.a
        C:/libs/ozz-animation-0.16.0-mingw/lib/libozz_options_r.a
        C:/libs/ozz-animation-0.16.0-mingw/lib/libozz_base_r.a
        box3d::box3d
    )
elseif (EMSCRIPTEN)
    target_link_libraries(app PRIVATE
        SDL3_image::SDL3_image
        SDL3_ttf::SDL3_ttf
        SDL3::SDL3
        # Point these to your WebAssembly-compiled ozz libraries (.a)
        C:/libs/ozz-animation-0.16.0-wasm/lib/libozz_animation_offline_r.a
        C:/libs/ozz-animation-0.16.0-wasm/lib/libozz_animation_r.a
        C:/libs/ozz-animation-0.16.0-wasm/lib/libozz_geometry_r.a
        C:/libs/ozz-animation-0.16.0-wasm/lib/libozz_options_r.a
        C:/libs/ozz-animation-0.16.0-wasm/lib/libozz_base_r.a
        box3d::box3d
    )
else()
    target_link_libraries(app PRIVATE
        SDL3_image::SDL3_image
        SDL3_ttf::SDL3_ttf
        SDL3::SDL3
        C:/libs/ozz-animation-0.16.0-android/arm64-v8a/lib/libozz_animation.a
        C:/libs/ozz-animation-0.16.0-android/arm64-v8a/lib/libozz_animation_offline.a
        C:/libs/ozz-animation-0.16.0-android/arm64-v8a/lib/libozz_geometry.a
        C:/libs/ozz-animation-0.16.0-android/arm64-v8a/lib/libozz_options.a
        C:/libs/ozz-animation-0.16.0-android/arm64-v8a/lib/libozz_base.a
        box3d::box3d
    )
endif()
