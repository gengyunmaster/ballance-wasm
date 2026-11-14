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

set(BUILD_TEST OFF CACHE BOOL "" FORCE)
set(INSTALL_DOCUMENTATION OFF CACHE BOOL "" FORCE)
set(INSTALL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(YYJSON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YYJSON_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${BML_WASM_UPSTREAM_DIR}/deps/oniguruma" oniguruma EXCLUDE_FROM_ALL)
add_subdirectory("${BML_WASM_UPSTREAM_DIR}/deps/yyjson" yyjson EXCLUDE_FROM_ALL)

set(BML_WASM_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
        "${BML_WASM_UPSTREAM_DIR}/include"
        "${BML_WASM_UPSTREAM_DIR}/src"
        "${BML_WASM_UPSTREAM_DIR}/src/Utils"
        "${BML_WASM_UPSTREAM_DIR}/deps/utf8.h"
        "${BML_WASM_UPSTREAM_DIR}/deps/imgui"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform"
)
set(BML_WASM_COMPILE_DEFINITIONS
        BML_EXPORT=
        BML_ENABLE_ANGELSCRIPT=0
        BML_ENABLE_UI_AUTOMATION=0
        IMGUI_USER_CONFIG="BMLImGuiConfig.h"
        IMGUI_EXPORT
        IniFile=BMLIniFile
)
set(BML_WASM_BEHAVIOR_SOURCES
        Behavior/Core/Layout.cpp
        Behavior/Core/Parameter.cpp
        Behavior/Core/Status.cpp
        Behavior/Core/Value.cpp
        Behavior/Engine/Access.cpp
        Behavior/Engine/Graph.cpp
        Behavior/Engine/Message.cpp
        Behavior/Engine/Pout.cpp
        Behavior/Install/Installations.cpp
        Behavior/Install/Selection.cpp
        Behavior/Block.cpp
        Behavior/Callback.cpp
        Behavior/CKEdit.cpp
        Behavior/CKGraph.cpp
        Behavior/CKPrototypeSource.cpp
        Behavior/Execution.cpp
        Behavior/FrameStore.cpp
        Behavior/Edit/Journal.cpp
        Behavior/Edit/Ops.cpp
        Behavior/Edit/Ops/Interface.cpp
        Behavior/Edit/Ops/Link.cpp
        Behavior/Edit/Ops/Overlay.cpp
        Behavior/Edit/Ops/Structure.cpp
        Behavior/Edit/Ops/Value.cpp
        Behavior/Edit/Program.cpp
        Behavior/Edit/Revert.cpp
        Behavior/Edit/Transaction.cpp
        Behavior/Lifecycle.cpp
        Behavior/Pattern.cpp
        Behavior/PrototypeCatalog.cpp
        Behavior/Relations.cpp
        Behavior/Runtime.cpp
        Behavior/Script.cpp
        Behavior/ScriptHelper.cpp
        Behavior/Sessions.cpp
        Behavior/Topology.cpp
        Behavior/Watch.cpp
        Behavior/Blocks/Hook.cpp
        Behavior/Blocks/HookBlock.cpp
        Behavior/Blocks/PhysicsForce.cpp
        Behavior/Blocks/Text2DView.cpp
)
set(BML_WASM_API_SOURCES
        Api/Behavior/Codec.cpp
        Api/Behavior/Edits.cpp
        Api/Behavior/Frames.cpp
        Api/Behavior/Payload.cpp
        Api/Behavior/Table.cpp
        Api/Behavior/Text.cpp
        Api/BmlApi.cpp
        Api/BuiltinCapabilities.cpp
        Api/CommandApi.cpp
        Api/Errors.cpp
        Api/ExecuteBB.cpp
        Api/ExecuteBBAdapter.cpp
        Api/IMod.cpp
        AngelScript/ScriptCommand.cpp
        Api/Interfaces.cpp
        Api/InterfaceRegistry.cpp
        Api/ObjectRefs.cpp
)
set(BML_WASM_CONSOLE_SOURCES
        Console/BuiltinCommands.cpp
        Console/CommandBar.cpp
        Console/CommandBarTheme.cpp
        Console/CommandContext.cpp
        Console/CommandInput.cpp
        Console/Console.cpp
        Console/ConsoleLayout.cpp
        Console/MessageBoard.cpp
        Console/Shell/FilterCommands.cpp
        Console/Shell/ShellBuiltins.cpp
        Console/Shell/ShellCompletion.cpp
        Console/Shell/ShellEditing.cpp
        Console/Shell/ShellEnvironment.cpp
        Console/Shell/ShellExecutor.cpp
        Console/Shell/ShellExpander.cpp
        Console/Shell/ShellHighlighter.cpp
        Console/Shell/ShellHistory.cpp
        Console/Shell/ShellIo.cpp
        Console/Shell/ShellLexer.cpp
        Console/Shell/ShellParser.cpp
        Console/Shell/ShellQuoting.cpp
)
set(BML_WASM_UI_SOURCES
        UI/AnsiPalette.cpp
        UI/AnsiText.cpp
        UI/Bui.cpp
        UI/ColorCodec.cpp
        UI/FontCommand.cpp
        UI/FontRuntime.cpp
        UI/GameFontCatalog.cpp
        UI/ImGuiStateRecovery.cpp
        UI/PaletteCommand.cpp
        UI/Gui/Button.cpp
        UI/Gui/Element.cpp
        UI/Gui/Gui.cpp
        UI/Gui/Input.cpp
        UI/Gui/KeyInput.cpp
        UI/Gui/Label.cpp
        UI/Gui/LegacyTextFont.cpp
        UI/Gui/Panel.cpp
        UI/Gui/Text.cpp
)

add_library(BMLWasmCore STATIC
        ModContext.cpp
        WasmBuiltinMod.cpp
        $<$<BOOL:${BALLANCE_WASM_BROWSER_TESTING}>:WasmBrowserTest.cpp>
        WasmCoreServices.cpp
        WasmImGuiBackend.cpp
        WasmInputHook.cpp
        WasmIvpRuntime.cpp
        WasmModRegistry.cpp
        WasmOverlay.cpp
        WasmPathUtils.cpp
        WasmPersistentStore.cpp
        WasmPlatform.cpp
        WasmRenderHooks.cpp
        "${BML_WASM_UPSTREAM_DIR}/src/Config/Config.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/Config/ConfigStore.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/CustomMaps/CustomMapLoad.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/CustomMaps/CustomMaps.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/CustomMaps/LevelLoader.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/CustomMaps/MapCatalog.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/CustomMaps/MapCommand.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/CustomMaps/MapMenu.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/CustomMaps/MapMenuState.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/CustomMaps/MapStaging.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/DataShare/DataShare.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/Gameplay/CheatCommand.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/Gameplay/GameEventHooks.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/Gameplay/GameplayTweaks.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/HUD/FpsCounter.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/HUD/HUD.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/HUD/HUDAnimation.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/HUD/HUDCommand.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/HUD/HUDRuntime.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/HUD/RealTimer.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/HUD/SRTimer.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/Imc/ImcApi.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/Imc/ImcRuntime.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/Logging/Logger.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/ModMenu/ModMenu.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/ModMenu/ModMenuModel.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/ModMenu/ModMenuPages.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/ModMenu/ModMenuPresentation.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/ModMenu/ModMenuSession.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/ModMenu/ModsMenuEntry.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/Mods/BMLCommand.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/Mods/BMLConfigMigration.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/Mods/BMLMod.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/Mods/NewBallTypeMod.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/Utils/HashUtils.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/Utils/IniFile.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/Utils/JsonUtils.cpp"
        "${BML_WASM_UPSTREAM_DIR}/src/Utils/StringUtils.cpp"
        "${BML_WASM_UPSTREAM_DIR}/deps/imgui/imgui.cpp"
        "${BML_WASM_UPSTREAM_DIR}/deps/imgui/imgui_draw.cpp"
        "${BML_WASM_UPSTREAM_DIR}/deps/imgui/imgui_tables.cpp"
        "${BML_WASM_UPSTREAM_DIR}/deps/imgui/imgui_widgets.cpp"
        "${BML_WASM_UPSTREAM_DIR}/deps/imgui/backends/imgui_impl_sdl3.cpp"
        "${BML_WASM_UPSTREAM_DIR}/deps/imgui/misc/cpp/imgui_stdlib.cpp"
)
foreach (source IN LISTS BML_WASM_BEHAVIOR_SOURCES BML_WASM_API_SOURCES BML_WASM_CONSOLE_SOURCES BML_WASM_UI_SOURCES)
    target_sources(BMLWasmCore PRIVATE "${BML_WASM_UPSTREAM_DIR}/src/${source}")
endforeach ()
target_compile_features(BMLWasmCore PUBLIC cxx_std_20)
target_compile_options(BMLWasmCore PRIVATE -fdeclspec -include "${CMAKE_CURRENT_SOURCE_DIR}/platform/Windows.h")
target_compile_definitions(BMLWasmCore
        PUBLIC ${BML_WASM_COMPILE_DEFINITIONS}
        PRIVATE BML_EXPORTS
)
if (BALLANCE_WASM_BROWSER_TESTING)
    target_compile_definitions(BMLWasmCore PRIVATE BALLANCE_WASM_BROWSER_TESTING=1)
endif ()
target_include_directories(BMLWasmCore PUBLIC
        ${BML_WASM_INCLUDE_DIRS}
        "${CMAKE_SOURCE_DIR}/upstream/ballanced/Source/RenderEngine/include"
        "${CMAKE_SOURCE_DIR}/upstream/ballanced/Source/BuildingBlocks/physics_RT"
        "${CMAKE_BINARY_DIR}/upstream/ballanced/Source/BuildingBlocks/physics_RT/ivp/include/ivp"
        "${BML_WASM_UPSTREAM_DIR}/mods/IVP/include"
        "${BML_WASM_UPSTREAM_DIR}/mods/IVP/src"
)
target_link_libraries(BMLWasmCore PUBLIC CK2Static SDL3::SDL3 PRIVATE onig yyjson physics_RTStatic)

function(bml_wasm_add_mod target directory entry_name)
    if (ARGN)
        set(sources ${ARGN})
    else ()
        set(sources "${directory}.cpp")
    endif ()
    list(TRANSFORM sources PREPEND "${BML_WASM_UPSTREAM_DIR}/mods/${directory}/")
    add_library(${target} OBJECT ${sources})
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_options(${target} PRIVATE -fdeclspec -include "${CMAKE_CURRENT_SOURCE_DIR}/platform/Windows.h")
    target_compile_definitions(${target} PRIVATE
            ${BML_WASM_COMPILE_DEFINITIONS}
            BMLEntry=BMLWasmCreate${entry_name}
            BMLExit=BMLWasmDestroy${entry_name}
    )
    target_include_directories(${target} PRIVATE
            ${BML_WASM_INCLUDE_DIRS}
            "${BML_WASM_UPSTREAM_DIR}/mods/${directory}"
    )
    target_link_libraries(${target} PRIVATE CK2Static)
    target_sources(BMLWasmCore PRIVATE $<TARGET_OBJECTS:${target}>)
endfunction()

bml_wasm_add_mod(BMLWasmModBallSticky BallSticky BallSticky)
bml_wasm_add_mod(BMLWasmModBMLModuls BMLModuls BMLModuls)
bml_wasm_add_mod(BMLWasmModCameraUtilities CameraUtilities CameraUtilities)
bml_wasm_add_mod(BMLWasmModDebugUtilities DebugUtilities DebugUtilities DebugUtilities.cpp DebugCommands.cpp)
bml_wasm_add_mod(BMLWasmModDeformedWB DeformedWB DeformedWB)
bml_wasm_add_mod(BMLWasmModDualBallControl DualBallControl DualBallControl)
bml_wasm_add_mod(BMLWasmModDynamicFov DynamicFov DynamicFov)
bml_wasm_add_mod(BMLWasmModMapScripts MapScripts MapScripts)
bml_wasm_add_mod(BMLWasmModNewSpawn NewSpawn NewSpawn)
bml_wasm_add_mod(BMLWasmModSegment Segment Segment
        Segment.cpp CommandSeg.cpp SegmentGui.cpp PerLevelSegmentState.cpp)
bml_wasm_add_mod(BMLWasmModSpiritTrail SpiritTrail SpiritTrail)
bml_wasm_add_mod(BMLWasmModTravelMode TravelMode TravelMode TravelMode.cpp TravelCommand.cpp)
bml_wasm_add_mod(BMLWasmModIVP IVP IVP src/Mod.cpp)
target_compile_definitions(BMLWasmModIVP PRIVATE IVP_VERSION="0.1.0")
target_include_directories(BMLWasmModIVP PRIVATE
        "${BML_WASM_UPSTREAM_DIR}/mods/IVP/include"
        "${BML_WASM_UPSTREAM_DIR}/mods/IVP/src"
)

add_library(BMLWasmStatic STATIC BMLWasmPlugin.cpp)
target_compile_features(BMLWasmStatic PRIVATE cxx_std_20)
target_compile_options(BMLWasmStatic PRIVATE -fdeclspec -include "${CMAKE_CURRENT_SOURCE_DIR}/platform/Windows.h")
target_compile_definitions(BMLWasmStatic PRIVATE ${BML_WASM_COMPILE_DEFINITIONS})
target_include_directories(BMLWasmStatic PRIVATE
        ${BML_WASM_INCLUDE_DIRS}
        "${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(BMLWasmStatic PRIVATE BMLWasmCore CK2Static)
