cmake_minimum_required(VERSION 3.29)

project(SecureBrowser LANGUAGES CXX)

# CMake identifica el sistema, el compilador y la arquitectura al ejecutar project().
if(NOT WIN32 OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC"
    OR NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
    message(FATAL_ERROR "SecureBrowser requires MSVC targeting Windows x64. Select the x64 toolchain.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# La aplicación conserva el flujo habitual; las pruebas se activan explícitamente.
option(SBP_BUILD_DESKTOP_APP "Build the Qt and CEF desktop application" ON)
option(BUILD_TESTING "Build the security core tests" OFF)
include(CTest)

# ICU se prepara explícitamente; la configuración utiliza únicamente el SDK local fijado.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/icu-sdk.json" SBP_ICU_MANIFEST)
string(JSON SBP_ICU_VERSION GET "${SBP_ICU_MANIFEST}" version)
string(JSON SBP_ICU_UNICODE_VERSION GET "${SBP_ICU_MANIFEST}" unicodeVersion)
string(JSON SBP_ICU_ARCHIVE GET "${SBP_ICU_MANIFEST}" archive)
get_filename_component(SBP_ICU_DIRECTORY_NAME "${SBP_ICU_ARCHIVE}" NAME_WE)
string(REPLACE "." ";" SBP_ICU_VERSION_PARTS "${SBP_ICU_VERSION}")
list(GET SBP_ICU_VERSION_PARTS 0 SBP_ICU_MAJOR_VERSION)
set(SBP_ICU_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/.deps/icu/${SBP_ICU_DIRECTORY_NAME}"
    CACHE PATH "Root directory of the approved ICU binary SDK"
)
foreach(SBP_ICU_REQUIRED_FILE IN ITEMS
    LICENSE include/unicode/uvernum.h include/unicode/uchar.h
    include/unicode/ustring.h include/unicode/ucasemap.h
    lib64/icuuc.lib lib64/icudt.lib
    bin64/icuuc${SBP_ICU_MAJOR_VERSION}.dll bin64/icudt${SBP_ICU_MAJOR_VERSION}.dll
)
    if(NOT EXISTS "${SBP_ICU_ROOT}/${SBP_ICU_REQUIRED_FILE}")
        message(FATAL_ERROR
            "ICU SDK not found or incomplete. Run scripts/setup-icu.ps1 "
            "or set -DSBP_ICU_ROOT=<the prepared SDK root>."
        )
    endif()
endforeach()
file(STRINGS "${SBP_ICU_ROOT}/include/unicode/uvernum.h" SBP_ICU_VERSION_LINE
    REGEX "^#define U_ICU_VERSION "
)
file(STRINGS "${SBP_ICU_ROOT}/include/unicode/uchar.h" SBP_ICU_UNICODE_VERSION_LINE
    REGEX "^#define U_UNICODE_VERSION "
)
if(NOT SBP_ICU_VERSION_LINE STREQUAL "#define U_ICU_VERSION \"${SBP_ICU_VERSION}\""
    OR NOT SBP_ICU_UNICODE_VERSION_LINE STREQUAL "#define U_UNICODE_VERSION \"${SBP_ICU_UNICODE_VERSION}\"")
    message(FATAL_ERROR "ICU SDK or Unicode version does not match icu-sdk.json.")
endif()

# El SDK contiene bibliotecas de importación .lib y las DLL precompiladas correspondientes.
# Se utilizará su API C para mantener la frontera sin objetos C++ compartidos entre runtimes.
add_library(sbp_icu_data SHARED IMPORTED)
set_target_properties(sbp_icu_data PROPERTIES
    IMPORTED_IMPLIB "${SBP_ICU_ROOT}/lib64/icudt.lib"
    IMPORTED_LOCATION "${SBP_ICU_ROOT}/bin64/icudt${SBP_ICU_MAJOR_VERSION}.dll"
)
add_library(sbp_icu_uc SHARED IMPORTED)
set_target_properties(sbp_icu_uc PROPERTIES
    IMPORTED_IMPLIB "${SBP_ICU_ROOT}/lib64/icuuc.lib"
    IMPORTED_LOCATION "${SBP_ICU_ROOT}/bin64/icuuc${SBP_ICU_MAJOR_VERSION}.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${SBP_ICU_ROOT}/include"
)
target_link_libraries(sbp_icu_uc INTERFACE sbp_icu_data)

# Prepara el runtime y sus avisos junto al consumidor, sin depender del PATH del equipo.
function(sbp_deploy_icu SBP_TARGET)
    add_custom_command(TARGET ${SBP_TARGET} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:sbp_icu_uc>" "$<TARGET_FILE:sbp_icu_data>"
            "$<TARGET_FILE_DIR:${SBP_TARGET}>"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${SBP_TARGET}>/icu-notices"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${SBP_ICU_ROOT}/LICENSE" "$<TARGET_FILE_DIR:${SBP_TARGET}>/icu-notices/LICENSE"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/icu-sdk.json"
            "$<TARGET_FILE_DIR:${SBP_TARGET}>/icu-notices/icu-sdk.json"
        COMMENT "Deploying ICU libraries and notices..."
        VERBATIM
    )
endfunction()

# El núcleo se compila sin enlazar Qt, CEF ni bibliotecas de persistencia o red.
add_library(sbp_security_core STATIC
    src/security/download_control_config.h
    src/security/download_control_config.cpp
    src/security/filter_mode.h
    src/security/policy_manager.h
    src/security/policy_manager.cpp
    src/security/policy_snapshot.h
    src/security/policy_snapshot.cpp
    src/security/policy_validation_error.h
    src/security/security_action.h
    src/security/security_context.h
    src/security/security_context.cpp
    src/security/security_reason.h
    src/security/security_decision.h
    src/security/security_decision.cpp
    src/security/url_filtering_config.h
    src/security/url_filtering_config.cpp
)
target_include_directories(sbp_security_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(sbp_security_core PRIVATE sbp_icu_uc)

if(BUILD_TESTING)
    # Utiliza las fuentes preparadas explícitamente por setup-googletest.ps1.
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/googletest.json" SBP_GOOGLETEST_MANIFEST)
    string(JSON SBP_GOOGLETEST_VERSION GET "${SBP_GOOGLETEST_MANIFEST}" version)
    set(SBP_GOOGLETEST_ROOT
        "${CMAKE_CURRENT_SOURCE_DIR}/.deps/googletest/googletest-${SBP_GOOGLETEST_VERSION}"
        CACHE PATH "Root directory of the approved GoogleTest source distribution"
    )
    foreach(SBP_GTEST_REQUIRED_FILE IN ITEMS
        CMakeLists.txt googletest/CMakeLists.txt
        googletest/include/gtest/gtest.h googletest/src/gtest-all.cc
        googletest/src/gtest_main.cc
    )
        if(NOT EXISTS "${SBP_GOOGLETEST_ROOT}/${SBP_GTEST_REQUIRED_FILE}")
            message(FATAL_ERROR
                "GoogleTest sources not found or incomplete. Run scripts/setup-googletest.ps1 "
                "or set -DSBP_GOOGLETEST_ROOT=<the prepared source root>."
            )
        endif()
    endforeach()

    # Rechaza una copia distinta de la versión fijada en el manifiesto.
    file(STRINGS "${SBP_GOOGLETEST_ROOT}/CMakeLists.txt" SBP_GTEST_VERSION_LINE
        REGEX "^set\\(GOOGLETEST_VERSION "
    )
    if(NOT SBP_GTEST_VERSION_LINE STREQUAL "set(GOOGLETEST_VERSION ${SBP_GOOGLETEST_VERSION})")
        message(FATAL_ERROR "GoogleTest source version does not match googletest.json.")
    endif()

    # Limita estas opciones a GoogleTest: biblioteca estática y el mismo CRT dinámico.
    block()
        set(BUILD_SHARED_LIBS OFF)
        set(BUILD_GMOCK OFF)
        set(INSTALL_GTEST OFF)
        set(GTEST_HAS_ABSL OFF)
        set(gtest_build_tests OFF)
        set(gtest_build_samples OFF)
        set(gtest_force_shared_crt ON)
        add_subdirectory("${SBP_GOOGLETEST_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/googletest"
            EXCLUDE_FROM_ALL
        )
    endblock()

    # GoogleTest proporciona main(); nuestras pruebas enlazan únicamente el núcleo y gtest.
    add_executable(sbp_security_tests
        tests/security/download_control_config_test.cpp
        tests/security/policy_manager_test.cpp
        tests/security/policy_snapshot_test.cpp
        tests/security/security_context_test.cpp
        tests/security/security_decision_test.cpp
        tests/security/url_filtering_config_test.cpp
    )
    target_link_libraries(sbp_security_tests PRIVATE sbp_security_core GTest::gtest_main)
    sbp_deploy_icu(sbp_security_tests)

    # Descubre también cada caso parametrizado al ejecutar CTest.
    include(GoogleTest)
    gtest_discover_tests(sbp_security_tests DISCOVERY_MODE PRE_TEST)
endif()

# Termina la configuración antes de buscar los SDK cuando solo se construye el núcleo.
if(NOT SBP_BUILD_DESKTOP_APP)
    return()
endif()

set(Qt6_DIR "" CACHE PATH "Directory containing Qt6Config.cmake for the approved Qt kit")

if(NOT Qt6_DIR OR NOT EXISTS "${Qt6_DIR}/Qt6Config.cmake")
    message(FATAL_ERROR
        "Qt 6.12.0 MSVC 2022 x64 is required. "
        "Configure with -DQt6_DIR=<Qt-kit>/lib/cmake/Qt6, "
        "pointing to the directory containing Qt6Config.cmake."
    )
endif()

find_package(Qt6 6.12.0 EXACT REQUIRED CONFIG COMPONENTS Widgets NO_DEFAULT_PATH)

# El SDK se prepara con setup-cef.ps1 y se selecciona mediante una ruta local.
set(CEF_ROOT "" CACHE PATH "Root directory of the approved CEF binary distribution")
if(NOT CEF_ROOT OR NOT IS_DIRECTORY "${CEF_ROOT}")
    message(FATAL_ERROR
        "CEF SDK not found. Run scripts/setup-cef.ps1 and configure with "
        "-DCEF_ROOT=<the SDK root printed by the script>."
    )
endif()

foreach(CEF_REQUIRED_FILE IN ITEMS
    include/cef_app.h include/cef_version.h include/cef_sandbox_win.h
    cmake/FindCEF.cmake cmake/cef_variables.cmake cmake/cef_macros.cmake
    libcef_dll/CMakeLists.txt
    Debug/libcef.lib Debug/bootstrap.exe Debug/libcef.dll
    Release/libcef.lib Release/bootstrap.exe Release/libcef.dll
)
    if(NOT EXISTS "${CEF_ROOT}/${CEF_REQUIRED_FILE}")
        message(FATAL_ERROR "CEF SDK is incomplete: missing ${CEF_ROOT}/${CEF_REQUIRED_FILE}")
    endif()
endforeach()

# La versión esperada procede del mismo manifiesto que utiliza la preparación.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/cef-sdk.json" SBP_CEF_MANIFEST)
string(JSON SBP_CEF_VERSION GET "${SBP_CEF_MANIFEST}" version)
file(STRINGS "${CEF_ROOT}/include/cef_version.h" SBP_CEF_VERSION_DEFINE
    REGEX "^#define CEF_VERSION "
)
if(NOT SBP_CEF_VERSION_DEFINE STREQUAL "#define CEF_VERSION \"${SBP_CEF_VERSION}\"")
    message(FATAL_ERROR "CEF SDK version does not match cef-sdk.json: expected ${SBP_CEF_VERSION}")
endif()

# Comprueba las cabeceras PE (Portable Executable) del SDK sin ejecutar sus binarios.
function(sbp_require_x64_binary SBP_BINARY_PATH)
    file(SIZE "${SBP_BINARY_PATH}" SBP_BINARY_SIZE)
    if(SBP_BINARY_SIZE LESS 64)
        message(FATAL_ERROR "CEF binary has an incomplete Windows header: ${SBP_BINARY_PATH}")
    endif()
    file(READ "${SBP_BINARY_PATH}" SBP_DOS_HEADER LIMIT 64 HEX)
    if(NOT SBP_DOS_HEADER MATCHES "^4d5a")
        message(FATAL_ERROR "CEF binary is not a Windows PE file: ${SBP_BINARY_PATH}")
    endif()

    # Los cuatro bytes de la posición 0x3C indican dónde comienza la cabecera PE.
    string(SUBSTRING "${SBP_DOS_HEADER}" 120 8 SBP_PE_OFFSET_HEX)
    string(REGEX REPLACE "^(..)(..)(..)(..)$" "\\4\\3\\2\\1" SBP_PE_OFFSET_HEX "${SBP_PE_OFFSET_HEX}")
    math(EXPR SBP_PE_OFFSET "0x${SBP_PE_OFFSET_HEX}")
    math(EXPR SBP_MAX_PE_OFFSET "${SBP_BINARY_SIZE} - 6")
    if(SBP_PE_OFFSET LESS 64 OR SBP_PE_OFFSET GREATER SBP_MAX_PE_OFFSET)
        message(FATAL_ERROR "CEF binary has an invalid PE header offset: ${SBP_BINARY_PATH}")
    endif()

    # Firma PE seguida del identificador x64 (0x8664), en el orden de bytes del archivo.
    file(READ "${SBP_BINARY_PATH}" SBP_PE_HEADER OFFSET ${SBP_PE_OFFSET} LIMIT 6 HEX)
    if(NOT SBP_PE_HEADER STREQUAL "504500006486")
        message(FATAL_ERROR "CEF SDK binary must be Windows x64: ${SBP_BINARY_PATH}")
    endif()
endfunction()

foreach(SBP_CEF_CONFIGURATION IN ITEMS Debug Release)
    foreach(SBP_CEF_BINARY IN ITEMS bootstrap.exe libcef.dll)
        sbp_require_x64_binary("${CEF_ROOT}/${SBP_CEF_CONFIGURATION}/${SBP_CEF_BINARY}")
    endforeach()
endforeach()

# Debe fijarse antes de cargar CEF: sus reglas añaden la variante /MDd en Debug.
set(CEF_RUNTIME_LIBRARY_FLAG "/MD" CACHE STRING "MSVC runtime used by the CEF wrapper" FORCE)
list(PREPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)

# Compila el adaptador oficial y mantiene sus salidas dentro del directorio de construcción.
add_subdirectory("${CEF_LIBCEF_DLL_WRAPPER_PATH}"
    "${CMAKE_CURRENT_BINARY_DIR}/libcef_dll_wrapper"
)

# Selecciona la biblioteca de enlace del motor precompilado según la configuración.
ADD_LOGICAL_TARGET(libcef_lib "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")

# Nuestra DLL implementa la entrada que invoca el bootstrap.
add_library(SecureBrowser SHARED
    src/app/app_entry_win.cpp
    src/app/browser_session.cpp
    src/app/browser_session.h
    src/app/browser_navigation.cpp
    src/app/browser_navigation.h
    src/app/browser_lifecycle.cpp
    src/app/browser_lifecycle.h
    src/app/browser_window_bridge.cpp
    src/app/browser_window_bridge.h
    src/browser/browser_app.cpp
    src/browser/browser_app.h
    src/browser/browser_client.cpp
    src/browser/browser_client.h
    src/browser/browser_history_visitor.cpp
    src/browser/browser_history_visitor.h
    src/diagnostics/diagnostics_log.cpp
    src/diagnostics/diagnostics_log.h
    src/ui/browser_host.cpp
    src/ui/browser_host.h
    src/ui/browser_window.cpp
    src/ui/browser_window.h
    src/ui/browser_window_ui.cpp
    src/ui/browser_window_ui.h
)
# La raíz src permite includes como "browser/browser_client.h" desde cualquier fuente del target.
target_include_directories(SecureBrowser PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CEF_INCLUDE_PATH}"
)
# Evita que las macros min/max de Windows interfieran con las funciones estándar usadas por CEF.
target_compile_definitions(SecureBrowser PRIVATE NOMINMAX)
# Registra la configuración elegida por el generador, también en builds multiconfiguración.
target_compile_definitions(SecureBrowser PRIVATE
    "SBP_BUILD_CONFIGURATION=\"$<CONFIG>\""
)
# La etiqueta de versión Qt importa datos de QtCore e impide su carga diferida en MSVC.
# La versión del kit ya se exige mediante find_package(Qt6 ... EXACT).
target_compile_definitions(SecureBrowser PRIVATE QT_NO_VERSION_TAGGING)
# Carga Qt al crear QApplication en el principal, después de CefExecuteProcess.
# Los iconos utilizan QtGui en ambas configuraciones; QKeyEvent también lo utiliza en Debug.
# Diferimos esas importaciones para no cargar Qt en los subprocesos al arrancar.
target_link_options(SecureBrowser PRIVATE
    "/DELAYLOAD:$<TARGET_FILE_NAME:Qt6::Core>"
    "/DELAYLOAD:$<TARGET_FILE_NAME:Qt6::Gui>"
    "/DELAYLOAD:$<TARGET_FILE_NAME:Qt6::Widgets>"
)
# Carga estas DLL de Windows al usarlas, después de distinguir los subprocesos de CEF.
# Contienen las funciones para obtener la carpeta del usuario y liberar su resultado.
target_link_options(SecureBrowser PRIVATE /DELAYLOAD:shell32.dll /DELAYLOAD:ole32.dll)
# Difiere ICU hasta su uso por el motor, conservando el arranque de los subprocesos CEF.
target_link_options(SecureBrowser PRIVATE "/DELAYLOAD:$<TARGET_FILE_NAME:sbp_icu_uc>")
target_link_libraries(SecureBrowser PRIVATE
    sbp_security_core
    Qt6::Widgets
    libcef_dll_wrapper
    libcef_lib
    ${CEF_STANDARD_LIBS}
)
sbp_deploy_icu(SecureBrowser)

# Copia el bootstrap de la configuración elegida junto a nuestra DLL y lo renombra.
COPY_SINGLE_FILE(SecureBrowser
    "${CEF_BINARY_DIR}/bootstrap.exe"
    "$<TARGET_FILE_DIR:SecureBrowser>/SecureBrowser.exe"
)

# Las listas y carpetas proceden de cef_variables.cmake; COPY_FILES, de cef_macros.cmake.
# Copia los binarios de Debug/Release y los recursos comunes junto a nuestra DLL.
COPY_FILES(SecureBrowser "${CEF_BINARY_FILES}"
    "${CEF_BINARY_DIR}" "$<TARGET_FILE_DIR:SecureBrowser>"
)
COPY_FILES(SecureBrowser "${CEF_RESOURCE_FILES}"
    "${CEF_RESOURCE_DIR}" "$<TARGET_FILE_DIR:SecureBrowser>"
)

# Conserva las instrucciones, la licencia y los créditos de la distribución CEF.
COPY_FILES(SecureBrowser "README.txt;LICENSE.txt;CREDITS.html"
    "${CEF_ROOT}" "$<TARGET_FILE_DIR:SecureBrowser>"
)

add_executable(sbp_toolchain_check main.cpp)
target_link_libraries(sbp_toolchain_check PRIVATE Qt6::Widgets)

# Conserva la comprobación independiente del kit junto a la aplicación principal.
add_dependencies(SecureBrowser sbp_toolchain_check)

# Estos targets de herramientas proceden del kit cargado por find_package(Qt6).
# Analiza ambos binarios y prepara Qt después de construir nuestra DLL.
add_custom_command(TARGET SecureBrowser POST_BUILD
    COMMAND Qt6::windeployqt
        --qtpaths "$<TARGET_FILE:Qt6::qtpaths>"
        "$<IF:$<CONFIG:Debug>,--debug,--release>"
        --no-compiler-runtime
        --no-system-d3d-compiler
        --no-system-dxc-compiler
        --dir "$<TARGET_FILE_DIR:SecureBrowser>"
        "$<TARGET_FILE:SecureBrowser>"
        "$<TARGET_FILE:sbp_toolchain_check>"
    COMMENT "Deploying Qt libraries and plugins..."
    VERBATIM
)

# Qt6_DIR apunta a <kit>/lib/cmake/Qt6; tres niveles arriba está la raíz del kit.
get_filename_component(SBP_QT_KIT_ROOT "${Qt6_DIR}/../../.." ABSOLUTE)
# El instalador oficial conserva sus textos comunes en <instalación-Qt>/Licenses.
get_filename_component(SBP_QT_DEFAULT_LICENSE_DIR "${SBP_QT_KIT_ROOT}/../../Licenses" ABSOLUTE)
set(SBP_QT_LICENSE_DIR "${SBP_QT_DEFAULT_LICENSE_DIR}" CACHE PATH "Directory containing Qt installer license texts")
if(NOT EXISTS "${SBP_QT_LICENSE_DIR}/LICENSE")
    message(FATAL_ERROR "Qt license texts not found. Set -DSBP_QT_LICENSE_DIR=<Qt installation>/Licenses.")
endif()
COPY_SINGLE_FILE(SecureBrowser
    "${SBP_QT_LICENSE_DIR}" "$<TARGET_FILE_DIR:SecureBrowser>/qt-notices/licenses"
)

# SBOM (Software Bill of Materials) conserva componentes, versiones, copyright y licencias.
# Estos módulos corresponden a las bibliotecas, plugins y traducciones desplegados.
foreach(SBP_QT_MODULE IN ITEMS qtbase qtsvg qttranslations)
    set(SBP_QT_SBOM_NAME "${SBP_QT_MODULE}-${Qt6_VERSION}.spdx.json")
    if(NOT EXISTS "${SBP_QT_KIT_ROOT}/sbom/${SBP_QT_SBOM_NAME}")
        message(FATAL_ERROR "Qt SBOM not found: ${SBP_QT_KIT_ROOT}/sbom/${SBP_QT_SBOM_NAME}")
    endif()
    COPY_SINGLE_FILE(SecureBrowser
        "${SBP_QT_KIT_ROOT}/sbom/${SBP_QT_SBOM_NAME}"
        "$<TARGET_FILE_DIR:SecureBrowser>/qt-notices/sbom/${SBP_QT_SBOM_NAME}"
    )
endforeach()

# La macro oficial de CEF consulta esta variable para localizar la salida del target.
set(CEF_TARGET_OUT_DIR "$<TARGET_FILE_DIR:SecureBrowser>")
# Permite lectura y ejecución a LPAC (Less Privileged AppContainer), con herencia.
# ACL significa Access Control List (lista de control de acceso).
SET_LPAC_ACLS(SecureBrowser)

# Prepara el inventario por configuración para las comprobaciones de Foundation.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/runtime_inventory.cmake")
sbp_generate_runtime_inventory(SecureBrowser)
