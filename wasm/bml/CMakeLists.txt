cmake_minimum_required(VERSION 3.25)

set(BML_WASM_UPSTREAM_DIR "${CMAKE_SOURCE_DIR}/upstream/ballancemodloaderplus")
file(STRINGS "${BML_WASM_UPSTREAM_DIR}/CMakeLists.txt" BML_WASM_VERSION_LINE
        REGEX "^[ \t]*VERSION[ \t]+[0-9]+\\.[0-9]+\\.[0-9]+")
if (NOT BML_WASM_VERSION_LINE)
    message(FATAL_ERROR "Unable to determine the pinned BML+ version")
endif ()
string(REGEX REPLACE ".*VERSION[ \t]+([0-9]+\\.[0-9]+\\.[0-9]+).*" "\\1"
        BML_WASM_VERSION "${BML_WASM_VERSION_LINE}")
string(REPLACE "." ";" BML_WASM_VERSION_PARTS "${BML_WASM_VERSION}")
list(GET BML_WASM_VERSION_PARTS 0 BML_VERSION_MAJOR)
list(GET BML_WASM_VERSION_PARTS 1 BML_VERSION_MINOR)
list(GET BML_WASM_VERSION_PARTS 2 BML_VERSION_PATCH)
set(BML_VERSION "${BML_WASM_VERSION}")

configure_file(BMLWasmVersion.h.in BMLWasmVersion.h @ONLY)
configure_file(
        "${BML_WASM_UPSTREAM_DIR}/src/Version.h.in"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/BML/Version.h"
        @ONLY
)
set_source_files_properties(
        "${BML_WASM_UPSTREAM_DIR}/src/Logging/Logger.cpp"
        PROPERTIES COMPILE_DEFINITIONS _DEBUG
)

set(BML_WASM_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
        "${BML_WASM_UPSTREAM_DIR}/include"
        "${BML_WASM_UPSTREAM_DIR}/src"
        "${BML_WASM_UPSTREAM_DIR}/src/Utils"
        "${BML_WASM_UPSTREAM_DIR}/deps/utf8.h"
        "${BML_WASM_UPSTREAM_DIR}/deps/imgui"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform"
)

add_library(BMLWasmCore STATIC
        BMLWasmApi.cpp
        ModContext.cpp
        WasmExecuteBB.cpp
        WasmGameEvents.cpp
        WasmInputHook.cpp
        WasmPathUtils.cpp
        "${BML_WASM_UPSTREAM_DIR}/src/Api/IMod.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/Behavior/Engine/Access.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/Behavior/Engine/Graph.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/Behavior/ScriptHelper.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/Config/Config.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/Config/ConfigStore.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/Logging/Logger.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/Utils/HashUtils.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/Utils/StringUtils.cpp"
)
target_compile_features(BMLWasmCore PUBLIC cxx_std_20)
target_compile_options(BMLWasmCore PRIVATE -fdeclspec)
target_compile_definitions(BMLWasmCore
        PUBLIC BML_EXPORT= BML_ENABLE_ANGELSCRIPT=0 BML_ENABLE_UI_AUTOMATION=0
        PRIVATE BML_EXPORTS
)
target_include_directories(BMLWasmCore PUBLIC ${BML_WASM_INCLUDE_DIRS})
target_link_libraries(BMLWasmCore PUBLIC CK2Static)

function(bml_wasm_add_mod target directory entry_name)
    add_library(${target} OBJECT
            "${BML_WASM_UPSTREAM_DIR}/mods/${directory}/${directory}.cpp"
    )
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_options(${target} PRIVATE -fdeclspec)
    target_compile_definitions(${target} PRIVATE
            BML_EXPORT= BML_ENABLE_ANGELSCRIPT=0 BML_ENABLE_UI_AUTOMATION=0
            BMLEntry=BMLWasmCreate${entry_name}
            BMLExit=BMLWasmDestroy${entry_name}
    )
    target_include_directories(${target} PRIVATE
            ${BML_WASM_INCLUDE_DIRS}
            "${BML_WASM_UPSTREAM_DIR}/mods/${directory}"
    )
    target_link_libraries(${target} PRIVATE CK2Static)
endfunction()

bml_wasm_add_mod(BMLWasmModMapScripts MapScripts MapScripts)
bml_wasm_add_mod(BMLWasmModDeformedWB DeformedWB DeformedWB)
bml_wasm_add_mod(BMLWasmModCameraUtilities CameraUtilities CameraUtilities)
target_sources(BMLWasmCore PRIVATE
        $<TARGET_OBJECTS:BMLWasmModMapScripts>
        $<TARGET_OBJECTS:BMLWasmModDeformedWB>
        $<TARGET_OBJECTS:BMLWasmModCameraUtilities>
)

add_library(BMLWasmStatic STATIC BMLWasmPlugin.cpp)
target_compile_features(BMLWasmStatic PRIVATE cxx_std_20)
target_include_directories(BMLWasmStatic PRIVATE
        ${BML_WASM_INCLUDE_DIRS}
        "${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(BMLWasmStatic PRIVATE BMLWasmCore CK2Static)
