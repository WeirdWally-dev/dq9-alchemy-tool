cmake_minimum_required(VERSION 3.21)

# The app's version. Release builds take it from the release tag (CI passes -DDQ9_VERSION=1.4.0 for
# tag v1.4.0), so this default only matters for builds that aren't releases.
set(DQ9_VERSION "1.3.2" CACHE STRING "App version (major.minor.patch)")
project(DQ9AlchemyHelper VERSION ${DQ9_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

option(DQ9_BUILD_TESTS "Build the test suite (desktop only)" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network)
qt_standard_project_setup()

# --------------------------------------------------------------------------- gumbo (HTML5 parser)
# Vendored from https://github.com/google/gumbo-parser (Apache-2.0), see third_party/gumbo/LICENSE.
file(GLOB GUMBO_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/gumbo/*.c)
add_library(gumbo STATIC ${GUMBO_SOURCES})
target_include_directories(gumbo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/gumbo)
set_target_properties(gumbo PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(gumbo PRIVATE -w)
elseif(MSVC)
    target_include_directories(gumbo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/gumbo/msvc)
    target_compile_options(gumbo PRIVATE /W0 /utf-8)
    target_compile_definitions(gumbo PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8>)   # the sources are UTF-8
endif()

# --------------------------------------------------------------------------- miniz (zip reader, for .xlsx)
# Vendored from https://github.com/richgel999/miniz 3.0.2 (MIT), see third_party/miniz/LICENSE.
add_library(miniz STATIC
    third_party/miniz/miniz.c third_party/miniz/miniz_tdef.c
    third_party/miniz/miniz_tinfl.c third_party/miniz/miniz_zip.c)
target_include_directories(miniz PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz)
target_compile_definitions(miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES MINIZ_NO_TIME)
set_target_properties(miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(miniz PRIVATE -w)
elseif(MSVC)
    target_compile_options(miniz PRIVATE /W0)
endif()

# --------------------------------------------------------------------------- core (no widgets)
# Everything except the GUI: parsing, caching, cross-referencing, the tree and the material list.
# The tests link against this, so they need no display.
add_library(dq9core STATIC
    src/core/text.h src/core/text.cpp
    src/core/html.h src/core/html.cpp
    src/core/models.h src/core/models.cpp
    src/core/parsers.h src/core/parsers.cpp
    src/core/wikitext.h src/core/wikitext.cpp
    src/core/robots.h src/core/robots.cpp
    src/core/http.h src/core/http.cpp
    src/core/store.h src/core/store.cpp
    src/core/registry.h src/core/registry.cpp
    src/core/render.h src/core/render.cpp
    src/core/tree.h src/core/tree.cpp
    src/core/xlsx.h src/core/xlsx.cpp
)
target_include_directories(dq9core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(dq9core PUBLIC Qt6::Core Qt6::Network gumbo miniz)
target_compile_definitions(dq9core PUBLIC APP_VERSION="${PROJECT_VERSION}")

qt_add_resources(dq9core "bundled_data"
    PREFIX "/data"
    BASE data
    FILES data/sheet_recipes.json data/shops_bundled.json data/icon.png
)

# --------------------------------------------------------------------------- app
qt_add_executable(dq9alchemy
    src/app/main.cpp
    src/app/theme.h src/app/theme.cpp
    src/app/treeview.h src/app/treeview.cpp
    src/app/materiallist.h src/app/materiallist.cpp
    src/app/workers.h src/app/workers.cpp
    src/app/mainwindow.h src/app/mainwindow.cpp
)
target_link_libraries(dq9alchemy PRIVATE dq9core Qt6::Widgets)
if(WIN32)
    target_sources(dq9alchemy PRIVATE packaging/windows/app.rc)   # exe icon
endif()
set_target_properties(dq9alchemy PROPERTIES
    OUTPUT_NAME "DQ9AlchemyHelper"
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

if(ANDROID)
    math(EXPR DQ9_VERSION_CODE "${PROJECT_VERSION_MAJOR} * 10000 + ${PROJECT_VERSION_MINOR} * 100 + ${PROJECT_VERSION_PATCH}")
    set_target_properties(dq9alchemy PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
        QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}"
        QT_ANDROID_VERSION_CODE "${DQ9_VERSION_CODE}"
        QT_ANDROID_MIN_SDK_VERSION 28
        QT_ANDROID_TARGET_SDK_VERSION 34
    )
    # HTTPS on Android needs OpenSSL shipped inside the APK (Qt's recommended way).
    include(FetchContent)
    FetchContent_Declare(android_openssl
        DOWNLOAD_EXTRACT_TIMESTAMP true
        URL https://github.com/KDAB/android_openssl/archive/refs/heads/master.zip)
    FetchContent_MakeAvailable(android_openssl)
    include(${android_openssl_SOURCE_DIR}/android_openssl.cmake)
    add_android_openssl_libraries(dq9alchemy)
endif()

# --------------------------------------------------------------------------- tests
if(DQ9_BUILD_TESTS AND NOT ANDROID)
    enable_testing()
    add_executable(dq9_tests tests/test_core.cpp)
    target_link_libraries(dq9_tests PRIVATE dq9core)
    qt_add_resources(dq9_tests "test_data"
        PREFIX "/tests"
        BASE tests
        FILES tests/weapon_wikitext.txt tests/recipes_sample.xlsx
    )
    add_test(NAME dq9_tests COMMAND dq9_tests)
endif()
