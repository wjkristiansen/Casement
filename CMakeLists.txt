# Casement - cross-platform window management
cmake_minimum_required(VERSION 3.24)

project(Casement VERSION 0.1.0 LANGUAGES C CXX)

option(CASEMENT_BUILD_SAMPLES "Build the Casement samples" ${PROJECT_IS_TOP_LEVEL})

if(UNIX AND NOT APPLE)
    option(CASEMENT_X11 "Build the X11 (xcb) backend" ON)
    option(CASEMENT_WAYLAND "Build the Wayland backend" ON)
    option(CASEMENT_LIBDECOR "Draw Wayland window decorations with libdecor where the compositor doesn't" ON)
endif()

add_library(Casement STATIC
    include/Casement/Casement.h
    src/Backends.h
    src/Casement.cpp
    src/Keys.cpp
)
add_library(Casement::Casement ALIAS Casement)

target_compile_features(Casement PUBLIC cxx_std_20)
target_include_directories(Casement
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MSVC)
    target_compile_options(Casement PRIVATE /W4 /permissive-)
else()
    # C++ only: the generated Wayland protocol code is C.
    target_compile_options(Casement PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic>)
endif()

#-------------------------------------------------------------------------------------------------
if(WIN32)
    target_sources(Casement PRIVATE src/Win32/Win32Display.cpp)
    target_compile_definitions(Casement PRIVATE CASEMENT_HAS_WIN32=1)
    target_link_libraries(Casement PUBLIC user32 gdi32)

#-------------------------------------------------------------------------------------------------
elseif(UNIX AND NOT APPLE)
    if(NOT CASEMENT_X11 AND NOT CASEMENT_WAYLAND)
        message(FATAL_ERROR "Casement: enable CASEMENT_X11, CASEMENT_WAYLAND, or both")
    endif()

    find_package(PkgConfig REQUIRED)

    if(CASEMENT_X11)
        pkg_check_modules(CASEMENT_XCB REQUIRED IMPORTED_TARGET xcb)
        target_sources(Casement PRIVATE src/X11/X11Display.cpp)
        target_compile_definitions(Casement PRIVATE CASEMENT_HAS_X11=1)
        target_link_libraries(Casement PRIVATE PkgConfig::CASEMENT_XCB)
    endif()

    if(CASEMENT_WAYLAND)
        pkg_check_modules(CASEMENT_WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client wayland-cursor)
        pkg_get_variable(CASEMENT_WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
        pkg_get_variable(CASEMENT_WAYLAND_SCANNER_HINT wayland-scanner wayland_scanner)
        find_program(CASEMENT_WAYLAND_SCANNER NAMES wayland-scanner HINTS ${CASEMENT_WAYLAND_SCANNER_HINT})
        if(NOT CASEMENT_WAYLAND_PROTOCOLS_DIR OR NOT CASEMENT_WAYLAND_SCANNER)
            message(FATAL_ERROR "Casement: the Wayland backend needs wayland-protocols and wayland-scanner, "
                "or set CASEMENT_WAYLAND=OFF")
        endif()

        set(generated_dir ${CMAKE_CURRENT_BINARY_DIR}/wayland-protocols)
        file(MAKE_DIRECTORY ${generated_dir})

        foreach(protocol
            stable/xdg-shell/xdg-shell.xml
            unstable/xdg-decoration/xdg-decoration-unstable-v1.xml
            unstable/relative-pointer/relative-pointer-unstable-v1.xml
            unstable/pointer-constraints/pointer-constraints-unstable-v1.xml
        )
            get_filename_component(name ${protocol} NAME_WE)
            set(xml ${CASEMENT_WAYLAND_PROTOCOLS_DIR}/${protocol})
            set(header ${generated_dir}/${name}-client-protocol.h)
            set(code ${generated_dir}/${name}-protocol.c)
            add_custom_command(
                OUTPUT ${header} ${code}
                COMMAND ${CASEMENT_WAYLAND_SCANNER} client-header ${xml} ${header}
                COMMAND ${CASEMENT_WAYLAND_SCANNER} private-code ${xml} ${code}
                DEPENDS ${xml}
                COMMENT "Generating Wayland protocol ${name}"
                VERBATIM
            )
            target_sources(Casement PRIVATE ${header} ${code})
        endforeach()

        target_sources(Casement PRIVATE src/Wayland/WaylandDisplay.cpp)
        target_include_directories(Casement PRIVATE ${generated_dir})
        target_compile_definitions(Casement PRIVATE CASEMENT_HAS_WAYLAND=1)
        target_link_libraries(Casement PRIVATE PkgConfig::CASEMENT_WAYLAND_CLIENT)

        if(CASEMENT_LIBDECOR)
            pkg_check_modules(CASEMENT_LIBDECOR_PKG IMPORTED_TARGET libdecor-0)
            if(NOT CASEMENT_LIBDECOR_PKG_FOUND)
                message(FATAL_ERROR "Casement: Wayland title bars need libdecor-0, or set CASEMENT_LIBDECOR=OFF")
            endif()
            target_compile_definitions(Casement PRIVATE CASEMENT_HAS_LIBDECOR=1)
            target_link_libraries(Casement PRIVATE PkgConfig::CASEMENT_LIBDECOR_PKG)
        endif()
    endif()

#-------------------------------------------------------------------------------------------------
else()
    message(FATAL_ERROR "Casement: no backend for ${CMAKE_SYSTEM_NAME} yet")
endif()

#-------------------------------------------------------------------------------------------------
if(CASEMENT_BUILD_SAMPLES)
    add_subdirectory(samples/Basic)
endif()
