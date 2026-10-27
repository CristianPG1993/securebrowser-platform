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
add_library(SecureBrowser SHARED app_entry_win.cpp)
target_include_directories(SecureBrowser PRIVATE "${CEF_INCLUDE_PATH}")
# Evita que las macros min/max de Windows interfieran con las funciones estándar usadas por CEF.
target_compile_definitions(SecureBrowser PRIVATE NOMINMAX)
# Carga estas DLL de Windows al usarlas, después de distinguir los subprocesos de CEF.
# Contienen las funciones para obtener la carpeta del usuario y liberar su resultado.
target_link_options(SecureBrowser PRIVATE /DELAYLOAD:shell32.dll /DELAYLOAD:ole32.dll)
target_link_libraries(SecureBrowser PRIVATE
    Qt6::Widgets
    libcef_dll_wrapper
    libcef_lib
    ${CEF_STANDARD_LIBS}
)

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

# La entrada de nuestra DLL aún no usa Qt; el ejecutable temporal aporta esas dependencias.
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
