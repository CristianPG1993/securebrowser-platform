# Describe los archivos de ejecución que comprobará la batería de Foundation.
# El inventario se genera dentro de build, con los nombres reales de cada configuración.
function(sbp_generate_runtime_inventory SBP_TARGET)
    set(SBP_RUNTIME_FILES
        "SecureBrowser.exe"
        "$<TARGET_FILE_NAME:${SBP_TARGET}>"
        "$<TARGET_FILE_NAME:Qt6::Core>"
        "$<TARGET_FILE_NAME:Qt6::Gui>"
        "$<TARGET_FILE_NAME:Qt6::Widgets>"
        # ICU aporta las operaciones Unicode y sus datos, junto a sus avisos de distribución.
        "$<TARGET_FILE_NAME:sbp_icu_uc>"
        "$<TARGET_FILE_NAME:sbp_icu_data>"
        "icu-notices/LICENSE"
        "icu-notices/icu-sdk.json"
        # El kit Windows fijado utiliza qwindowsd.dll en Debug y qwindows.dll en Release.
        "platforms/qwindows$<$<CONFIG:Debug>:d>.dll"
        ${CEF_BINARY_FILES}
    )

    # COPY_FILES copia también carpetas; enumeramos su contenido para detectar archivos ausentes.
    foreach(SBP_RESOURCE IN LISTS CEF_RESOURCE_FILES)
        if(IS_DIRECTORY "${CEF_RESOURCE_DIR}/${SBP_RESOURCE}")
            file(GLOB_RECURSE SBP_RESOURCE_CONTENT LIST_DIRECTORIES false
                RELATIVE "${CEF_RESOURCE_DIR}"
                "${CEF_RESOURCE_DIR}/${SBP_RESOURCE}/*"
            )
            if(NOT SBP_RESOURCE_CONTENT)
                message(FATAL_ERROR "CEF runtime resource directory is empty: ${SBP_RESOURCE}")
            endif()
            list(APPEND SBP_RUNTIME_FILES ${SBP_RESOURCE_CONTENT})
        else()
            list(APPEND SBP_RUNTIME_FILES "${SBP_RESOURCE}")
        endif()
    endforeach()

    # Una lista estable permite revisar el inventario y reutilizarlo desde los comprobadores.
    list(REMOVE_DUPLICATES SBP_RUNTIME_FILES)
    list(SORT SBP_RUNTIME_FILES)
    list(JOIN SBP_RUNTIME_FILES "\",\n    \"" SBP_RUNTIME_FILES_JSON)
    string(CONCAT SBP_RUNTIME_INVENTORY
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"configuration\": \"$<CONFIG>\",\n"
        "  \"runtime_directory\": \"$<TARGET_FILE_DIR:${SBP_TARGET}>\",\n"
        "  \"required_files\": [\n    \"${SBP_RUNTIME_FILES_JSON}\"\n  ]\n"
        "}\n"
    )
    # GENERATE resuelve los nombres Debug/Release al generar los proyectos de Visual Studio.
    file(GENERATE
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/runtime-inventory-$<CONFIG>.json"
        CONTENT "${SBP_RUNTIME_INVENTORY}"
        TARGET ${SBP_TARGET}
        NEWLINE_STYLE LF
    )
endfunction()
