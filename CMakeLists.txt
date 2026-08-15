cmake_minimum_required(VERSION 3.21.1)

project(c_nbt_explorer VERSION 0.2.0 LANGUAGES C)

option(NBT_EXPLORER_FETCH_ZLIB "Download a pinned zlib release when no system package is available" ON)
option(NBT_EXPLORER_ENABLE_WARNINGS "Enable the compiler's useful warning set" ON)
option(NBT_EXPLORER_BUILD_CLI "Build the command-line application" ON)
option(NBT_EXPLORER_BUILD_DESKTOP "Build the native Qt 6 Widgets application" OFF)
option(NBT_EXPLORER_BUNDLE_BEDROCK_LEVELDB
    "Statically bundle the Bedrock-compatible Amulet LevelDB backend in desktop builds" ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(NBT_EXPLORER_BEDROCK_LEVELDB_ENABLED OFF)
if(NBT_EXPLORER_BUILD_DESKTOP AND NBT_EXPLORER_BUNDLE_BEDROCK_LEVELDB)
    enable_language(CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    include(FetchContent)

    # Amulet LevelDB pins this zstd release but its upstream declaration has
    # no checksum. FetchContent keeps the first declaration, so add the hash
    # before the LevelDB project declares the same dependency.
    FetchContent_Declare(
        zstd
        URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
        URL_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR build/cmake
    )

    set(LEVELDB_BUILD_UTILS OFF CACHE BOOL "" FORCE)
    set(LEVELDB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(LEVELDB_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(LEVELDB_INSTALL OFF CACHE BOOL "" FORCE)
    set(LEVELDB_DISABLE_EXCEPTIONS ON CACHE BOOL "" FORCE)
    set(LEVELDB_DISABLE_RTTI ON CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
    # zlib 1.3.1 predates ZLIB_INSTALL and otherwise emits absolute install
    # rules based on the dependency's configure-time prefix.
    set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)

    set(NBT_EXPLORER_SAVED_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")
    set(NBT_EXPLORER_SAVED_POSITION_INDEPENDENT_CODE "${CMAKE_POSITION_INDEPENDENT_CODE}")
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(NBT_EXPLORER_LEVELDB_FETCH_ARGS
        GIT_REPOSITORY https://github.com/Amulet-Team/leveldb.git
        GIT_TAG 1352243bb27d287b27be94f7591218dddb3ef900
        GIT_SHALLOW FALSE
    )
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
        list(APPEND NBT_EXPLORER_LEVELDB_FETCH_ARGS EXCLUDE_FROM_ALL)
    endif()
    FetchContent_Declare(leveldb ${NBT_EXPLORER_LEVELDB_FETCH_ARGS})
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
        FetchContent_MakeAvailable(leveldb)
    else()
        # FetchContent gained EXCLUDE_FROM_ALL in 3.28. Preserve the same
        # no-install behavior on the project's older supported CMake versions.
        FetchContent_GetProperties(leveldb)
        if(NOT leveldb_POPULATED)
            FetchContent_Populate(leveldb)
            add_subdirectory("${leveldb_SOURCE_DIR}" "${leveldb_BINARY_DIR}"
                EXCLUDE_FROM_ALL)
        endif()
    endif()
    set(BUILD_SHARED_LIBS "${NBT_EXPLORER_SAVED_BUILD_SHARED_LIBS}" CACHE BOOL "" FORCE)
    set(CMAKE_POSITION_INDEPENDENT_CODE "${NBT_EXPLORER_SAVED_POSITION_INDEPENDENT_CODE}")

    if(NOT TARGET leveldb OR NOT TARGET zlibstatic)
        message(FATAL_ERROR "The pinned Amulet LevelDB build did not provide its expected targets")
    endif()
    set(NBT_EXPLORER_BEDROCK_LEVELDB_ENABLED ON)
    set(NBT_EXPLORER_ZLIB_TARGET zlibstatic)
endif()

if(NOT NBT_EXPLORER_BEDROCK_LEVELDB_ENABLED)
    find_package(ZLIB QUIET)
    if(ZLIB_FOUND)
        set(NBT_EXPLORER_ZLIB_TARGET ZLIB::ZLIB)
    elseif(NBT_EXPLORER_FETCH_ZLIB)
        include(FetchContent)
        set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
        set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
        set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
        set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            nbt_explorer_zlib
            URL https://github.com/madler/zlib/archive/refs/tags/v1.3.2.tar.gz
            URL_HASH SHA256=b99a0b86c0ba9360ec7e78c4f1e43b1cbdf1e6936c8fa0f6835c0cd694a495a1
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_MakeAvailable(nbt_explorer_zlib)

        if(TARGET zlibstatic)
            set(NBT_EXPLORER_ZLIB_TARGET zlibstatic)
        elseif(TARGET zlib)
            set(NBT_EXPLORER_ZLIB_TARGET zlib)
        else()
            message(FATAL_ERROR "The fetched zlib project did not provide a supported CMake target")
        endif()
    else()
        message(FATAL_ERROR "zlib was not found. Install zlib or configure with -DNBT_EXPLORER_FETCH_ZLIB=ON")
    endif()
endif()

file(GLOB NBT_EXPLORER_CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
list(REMOVE_ITEM NBT_EXPLORER_CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.c")

add_library(nbt_core STATIC ${NBT_EXPLORER_CORE_SOURCES})
target_include_directories(nbt_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/h")
target_link_libraries(nbt_core
    PUBLIC ${NBT_EXPLORER_ZLIB_TARGET}
    PRIVATE ${CMAKE_DL_LIBS}
)
if(NBT_EXPLORER_BEDROCK_LEVELDB_ENABLED)
    target_compile_definitions(nbt_core PRIVATE NBT_EXPLORER_BUNDLED_LEVELDB=1)
    target_link_libraries(nbt_core PRIVATE leveldb)
endif()
set_target_properties(nbt_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(NBT_EXPLORER_BUILD_CLI)
    add_executable(nbt_explorer "${CMAKE_CURRENT_SOURCE_DIR}/src/main.c")
    target_compile_definitions(nbt_explorer PRIVATE CNBT_VERSION="${PROJECT_VERSION}")
    target_link_libraries(nbt_explorer PRIVATE nbt_core)
endif()

if(WIN32)
    target_compile_definitions(nbt_core PUBLIC
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
endif()

if(NBT_EXPLORER_ENABLE_WARNINGS)
    if(MSVC)
        target_compile_options(nbt_core PRIVATE /W4 /permissive- /utf-8)
        if(TARGET nbt_explorer)
            target_compile_options(nbt_explorer PRIVATE /W4 /permissive- /utf-8)
        endif()
    else()
        target_compile_options(nbt_core PRIVATE -Wall -Wextra -Wpedantic)
        if(TARGET nbt_explorer)
            target_compile_options(nbt_explorer PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endif()
endif()

# Keep the executable location identical for single- and multi-config generators.
if(TARGET nbt_explorer)
    set_target_properties(nbt_explorer PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set_target_properties(nbt_explorer PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${config}" "${CMAKE_BINARY_DIR}/bin"
        )
    endforeach()
endif()

include(GNUInstallDirs)

# App-local MSVC runtimes keep both the portable ZIP and NSIS package usable
# on clean Windows systems that do not already have the Visual C++
# Redistributable installed. InstallRequiredSystemLibraries selects the
# redistributable DLLs for the compiler/toolset used to build this package.
if(MSVC AND NBT_EXPLORER_BUILD_DESKTOP)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "${CMAKE_INSTALL_BINDIR}")
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT desktop)
    include(InstallRequiredSystemLibraries)
endif()

if(TARGET nbt_explorer)
    install(TARGETS nbt_explorer
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        COMPONENT cli
    )
endif()

if(NBT_EXPLORER_BUILD_DESKTOP)
    set(NBT_EXPLORER_GUI_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/gui/main.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/gui/MainWindow.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/gui/MainWindow.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/gui/NbtTreeModel.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/gui/NbtTreeModel.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/gui/Document.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/gui/Document.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/gui/BedrockDatabaseDialog.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/gui/BedrockDatabaseDialog.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/gui/resources.qrc"
    )
    foreach(gui_source IN LISTS NBT_EXPLORER_GUI_SOURCES)
        if(NOT EXISTS "${gui_source}")
            message(FATAL_ERROR "Desktop build requested, but ${gui_source} is missing")
        endif()
    endforeach()

    enable_language(CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)

    # Some Intel Qt 6 SDKs still advertise the long-removed AGL framework.
    # Modern macOS SDKs no longer ship it, and Qt Widgets does not require it.
    if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
        get_target_property(NBT_EXPLORER_OPENGL_LINKS
            WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
        if(NBT_EXPLORER_OPENGL_LINKS)
            list(FILTER NBT_EXPLORER_OPENGL_LINKS EXCLUDE REGEX "AGL")
            set_property(TARGET WrapOpenGL::WrapOpenGL PROPERTY
                INTERFACE_LINK_LIBRARIES "${NBT_EXPLORER_OPENGL_LINKS}")
        endif()
    endif()
    qt_standard_project_setup()
    set(CMAKE_AUTORCC ON)

    qt_add_executable(cnbt_explorer
        WIN32
        MACOSX_BUNDLE
        ${NBT_EXPLORER_GUI_SOURCES}
    )
    target_include_directories(cnbt_explorer PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/gui"
        "${CMAKE_CURRENT_SOURCE_DIR}/h"
    )
    target_link_libraries(cnbt_explorer PRIVATE nbt_core Qt6::Widgets)
    target_compile_definitions(cnbt_explorer PRIVATE CNBT_VERSION="${PROJECT_VERSION}")

    if(APPLE AND NBT_EXPLORER_BEDROCK_LEVELDB_ENABLED)
        set(NBT_EXPLORER_THIRD_PARTY_NOTICES
            "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.txt")
        target_sources(cnbt_explorer PRIVATE "${NBT_EXPLORER_THIRD_PARTY_NOTICES}")
        set_source_files_properties("${NBT_EXPLORER_THIRD_PARTY_NOTICES}" PROPERTIES
            MACOSX_PACKAGE_LOCATION Resources)
    endif()

    if(APPLE)
        set(NBT_EXPLORER_MAC_ICON "${CMAKE_CURRENT_SOURCE_DIR}/packaging/cnbt-explorer.icns")
        target_sources(cnbt_explorer PRIVATE "${NBT_EXPLORER_MAC_ICON}")
        set_source_files_properties("${NBT_EXPLORER_MAC_ICON}" PROPERTIES
            MACOSX_PACKAGE_LOCATION Resources)
    elseif(WIN32)
        target_sources(cnbt_explorer PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/packaging/cnbt-explorer.rc")
    endif()

    if(MSVC AND NBT_EXPLORER_ENABLE_WARNINGS)
        target_compile_options(cnbt_explorer PRIVATE /W4 /permissive- /utf-8)
    elseif(NBT_EXPLORER_ENABLE_WARNINGS)
        target_compile_options(cnbt_explorer PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    set_target_properties(cnbt_explorer PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "C-NBT Explorer"
        MACOSX_BUNDLE_ICON_FILE "cnbt-explorer.icns"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.cnbt-explorer"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        QT_WINDOWS_APP_PROJECT_IDENTIFIER "io.github.cnbt-explorer"
        QT_WINDOWS_APP_PROJECT_EXECUTION_LEVEL "asInvoker"
    )
    if(APPLE)
        set_target_properties(cnbt_explorer PROPERTIES OUTPUT_NAME "C-NBT Explorer")
    else()
        set_target_properties(cnbt_explorer PROPERTIES OUTPUT_NAME "cnbt-explorer")
    endif()

    install(TARGETS cnbt_explorer
        BUNDLE DESTINATION . COMPONENT desktop
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT desktop
    )
    if(NBT_EXPLORER_BEDROCK_LEVELDB_ENABLED AND NOT APPLE)
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.txt"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/c-nbt-explorer"
            COMPONENT desktop)
    endif()

    # Qt 6.9's convenience generator does not quote macOS bundle names that
    # contain spaces. Generate the equivalent quoted deployment call on macOS.
    if(APPLE)
        if(Qt6_VERSION VERSION_GREATER_EQUAL 6.7)
            qt_generate_deploy_script(
                TARGET cnbt_explorer
                OUTPUT_SCRIPT NBT_EXPLORER_QT_DEPLOY_SCRIPT
                CONTENT [[
qt_deploy_runtime_dependencies(
    EXECUTABLE "C-NBT Explorer.app"
    DEPLOY_TOOL_OPTIONS "-no-strip"
    NO_APP_STORE_COMPLIANCE
)
]]
            )
        else()
            qt_generate_deploy_script(
                TARGET cnbt_explorer
                OUTPUT_SCRIPT NBT_EXPLORER_QT_DEPLOY_SCRIPT
                CONTENT [[
qt_deploy_runtime_dependencies(
    EXECUTABLE "C-NBT Explorer.app"
    NO_APP_STORE_COMPLIANCE
)
]]
            )
        endif()
    else()
        qt_generate_deploy_app_script(
            TARGET cnbt_explorer
            OUTPUT_SCRIPT NBT_EXPLORER_QT_DEPLOY_SCRIPT
            NO_UNSUPPORTED_PLATFORM_ERROR
        )
    endif()
    install(SCRIPT "${NBT_EXPLORER_QT_DEPLOY_SCRIPT}" COMPONENT desktop)

    if(UNIX AND NOT APPLE)
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/io.github.cnbt-explorer.desktop"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/applications" COMPONENT desktop)
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/io.github.cnbt-explorer.svg"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps" COMPONENT desktop)
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/io.github.cnbt-explorer.xml"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/mime/packages" COMPONENT desktop)
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(platform_io_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/platform_io_test.c")
    target_link_libraries(platform_io_test PRIVATE nbt_core)
    add_test(
        NAME platform_io
        COMMAND platform_io_test "${CMAKE_CURRENT_BINARY_DIR}"
    )

    add_executable(extended_formats_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_extended_formats.c")
    target_link_libraries(extended_formats_test PRIVATE nbt_core)
    add_test(NAME extended_formats COMMAND extended_formats_test)

    if(NBT_EXPLORER_BEDROCK_LEVELDB_ENABLED)
        add_executable(bedrock_db_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_bedrock_db.c")
        target_compile_definitions(bedrock_db_test PRIVATE
            NBT_EXPLORER_TEST_BUNDLED_LEVELDB=1)
        target_link_libraries(bedrock_db_test PRIVATE nbt_core leveldb)
        add_test(
            NAME bedrock_db
            COMMAND bedrock_db_test "${CMAKE_CURRENT_BINARY_DIR}/bedrock_db_test_fixture"
        )
    endif()

    add_executable(region_lz4_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_region_lz4.c")
    target_link_libraries(region_lz4_test PRIVATE nbt_core)
    add_test(NAME region_lz4 COMMAND region_lz4_test)

    if(TARGET nbt_explorer)
        add_test(NAME cli_version COMMAND "$<TARGET_FILE:nbt_explorer>" --version)
        set_tests_properties(cli_version PROPERTIES
            PASS_REGULAR_EXPRESSION "C-NBT Explorer ${PROJECT_VERSION}")
        add_test(
            NAME cli_usage
            COMMAND "${CMAKE_COMMAND}"
                "-DPROGRAM=$<TARGET_FILE:nbt_explorer>"
                "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli_workflow_test"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/cli_usage.cmake"
        )
    endif()

    if(TARGET cnbt_explorer)
        add_executable(gui_document_test
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_document_test.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/gui/Document.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/gui/Document.h"
        )
        target_include_directories(gui_document_test PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/gui"
            "${CMAKE_CURRENT_SOURCE_DIR}/h"
        )
        target_link_libraries(gui_document_test PRIVATE nbt_core Qt6::Widgets)
        if(NBT_EXPLORER_BEDROCK_LEVELDB_ENABLED)
            target_compile_definitions(gui_document_test PRIVATE
                NBT_EXPLORER_TEST_BUNDLED_LEVELDB=1)
            target_link_libraries(gui_document_test PRIVATE leveldb)
        endif()
        add_test(NAME gui_document COMMAND gui_document_test)
    endif()
endif()

set(CPACK_PACKAGE_NAME "C-NBT Explorer")
set(CPACK_PACKAGE_VENDOR "C-NBT Explorer contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Native cross-platform Minecraft NBT explorer and editor")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "C-NBT Explorer")

set(NBT_EXPLORER_PACKAGE_PROCESSOR "${CMAKE_SYSTEM_PROCESSOR}")
if(WIN32 AND NBT_EXPLORER_PACKAGE_PROCESSOR MATCHES "^(AMD64|amd64|x86_64)$")
    set(NBT_EXPLORER_PACKAGE_PROCESSOR "x86_64")
endif()
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    set(NBT_EXPLORER_MAC_ARCHITECTURES ${CMAKE_OSX_ARCHITECTURES})
    list(FIND NBT_EXPLORER_MAC_ARCHITECTURES arm64 NBT_EXPLORER_HAS_ARM64)
    list(FIND NBT_EXPLORER_MAC_ARCHITECTURES x86_64 NBT_EXPLORER_HAS_X86_64)
    if(NOT NBT_EXPLORER_HAS_ARM64 EQUAL -1 AND
       NOT NBT_EXPLORER_HAS_X86_64 EQUAL -1)
        set(NBT_EXPLORER_PACKAGE_PROCESSOR "universal")
    else()
        string(REPLACE ";" "-" NBT_EXPLORER_PACKAGE_PROCESSOR
            "${CMAKE_OSX_ARCHITECTURES}")
    endif()
endif()
set(CPACK_PACKAGE_FILE_NAME
    "C-NBT-Explorer-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${NBT_EXPLORER_PACKAGE_PROCESSOR}")
if(NBT_EXPLORER_BUILD_DESKTOP)
    set(CPACK_COMPONENTS_ALL desktop)
else()
    set(CPACK_COMPONENTS_ALL cli)
endif()

if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "C-NBT Explorer")
elseif(WIN32)
    if(NBT_EXPLORER_BUILD_DESKTOP)
        set(CPACK_GENERATOR "ZIP;NSIS")
        set(CPACK_NSIS_DISPLAY_NAME "C-NBT Explorer")
        set(CPACK_NSIS_PACKAGE_NAME "C-NBT Explorer")
        set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
        set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/packaging/cnbt-explorer.ico")
        set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}/packaging/cnbt-explorer.ico")
        set(CPACK_PACKAGE_EXECUTABLES "cnbt-explorer" "C-NBT Explorer")
        # This separate CPack config keeps NSIS' nested quoting and backslashes
        # out of the generated CPackConfig.cmake string literals.
        set(CPACK_PROJECT_CONFIG_FILE
            "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/CPackOptions.cmake")
    else()
        set(CPACK_GENERATOR "ZIP")
    endif()
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
