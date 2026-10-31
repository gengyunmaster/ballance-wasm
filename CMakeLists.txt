cmake_minimum_required(VERSION 3.25)

project(BallanceWasm LANGUAGES C CXX)

set(BALLANCE_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(BALLANCE_WASM_PATCH_ROOT "${CMAKE_SOURCE_DIR}/dependencies")

add_subdirectory(upstream/ballanced upstream/ballanced)

if (EMSCRIPTEN)
    if (NOT BALLANCE_ASSETS_ROOT)
        message(FATAL_ERROR "BALLANCE_ASSETS_ROOT must point to a local Ballance game directory")
    endif ()

    find_file(BALLANCE_FONT_FILE
            NAMES LiberationSans-Regular.ttf
            PATHS "${CMAKE_SOURCE_DIR}/assets/fonts" "/usr/share/fonts/truetype/liberation"
            NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    if (NOT BALLANCE_FONT_FILE)
        message(FATAL_ERROR "Liberation Sans is required; set BALLANCE_FONT_FILE to its path")
    endif ()
    find_file(BALLANCE_FONT_LICENSE_FILE
            NAMES copyright
            PATHS "/usr/share/doc/fonts-liberation"
            NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)

    set(WEB_ASSET_ROOT "${CMAKE_BINARY_DIR}/web-assets")
    file(MAKE_DIRECTORY "${WEB_ASSET_ROOT}" "${WEB_ASSET_ROOT}/Fonts")
    configure_file(web/Player.ini "${WEB_ASSET_ROOT}/Player.ini" COPYONLY)
    configure_file(web/CKBgfxRasterizer.ini "${WEB_ASSET_ROOT}/CKBgfxRasterizer.ini" COPYONLY)
    configure_file("${BALLANCE_FONT_FILE}" "${WEB_ASSET_ROOT}/Fonts/LiberationSans-Regular.ttf" COPYONLY)
    if (BALLANCE_FONT_LICENSE_FILE)
        configure_file("${BALLANCE_FONT_LICENSE_FILE}" "${WEB_ASSET_ROOT}/Fonts/LICENSE-Liberation.txt" COPYONLY)
    endif ()

    add_custom_target(BallanceWebAssets
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${BALLANCE_ASSETS_ROOT}/base.cmo" "${WEB_ASSET_ROOT}/base.cmo"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${BALLANCE_ASSETS_ROOT}/Database.tdb" "${WEB_ASSET_ROOT}/Database.tdb"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                    "${BALLANCE_ASSETS_ROOT}/Textures" "${WEB_ASSET_ROOT}/Textures"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                    "${BALLANCE_ASSETS_ROOT}/Sounds" "${WEB_ASSET_ROOT}/Sounds"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                    "${BALLANCE_ASSETS_ROOT}/Text" "${WEB_ASSET_ROOT}/Text"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                    "${BALLANCE_ASSETS_ROOT}/3D Entities" "${WEB_ASSET_ROOT}/3D Entities"
    )
    add_dependencies(Player BallanceWebAssets)

    file(GLOB_RECURSE WEB_ASSET_DEPENDENCIES CONFIGURE_DEPENDS
            "${WEB_ASSET_ROOT}/Player.ini"
            "${WEB_ASSET_ROOT}/CKBgfxRasterizer.ini"
            "${BALLANCE_ASSETS_ROOT}/base.cmo"
            "${BALLANCE_ASSETS_ROOT}/Database.tdb"
            "${BALLANCE_ASSETS_ROOT}/Textures/*"
            "${BALLANCE_ASSETS_ROOT}/Sounds/*"
            "${BALLANCE_ASSETS_ROOT}/Text/*"
            "${BALLANCE_ASSETS_ROOT}/3D Entities/*"
    )
    list(APPEND WEB_ASSET_DEPENDENCIES "${BALLANCE_FONT_FILE}")
    if (BALLANCE_FONT_LICENSE_FILE)
        list(APPEND WEB_ASSET_DEPENDENCIES "${BALLANCE_FONT_LICENSE_FILE}")
    endif ()
    set_property(TARGET Player APPEND PROPERTY LINK_DEPENDS "${WEB_ASSET_DEPENDENCIES}")

    target_link_options(Player PRIVATE
            "SHELL:--preload-file ${WEB_ASSET_ROOT}@/"
            "-sALLOW_MEMORY_GROWTH=1"
            "-sINITIAL_MEMORY=268435456"
            "-sNODE_HOST_ENV=1"
            "-sUSE_WEBGL2=1"
            "-sFULL_ES3=1"
            "-sPTHREAD_POOL_SIZE=4"
            "-pthread"
    )

    install(FILES
            "$<TARGET_FILE_DIR:Player>/Player.wasm"
            "$<TARGET_FILE_DIR:Player>/Player.data"
            "${CMAKE_SOURCE_DIR}/web/index.html"
            DESTINATION Bin
            COMPONENT Runtime
    )
endif ()
