cmake_minimum_required(VERSION 3.29)

# Este script se ejecuta desde CTest y permanece fuera del proceso que carga CEF.
if(NOT DEFINED SBP_CEF_TEST_EXECUTABLE OR NOT EXISTS "${SBP_CEF_TEST_EXECUTABLE}")
    message(FATAL_ERROR "CEF adapter test executable not found. Build sbp_cef_adapter_tests first.")
endif()
file(REAL_PATH "${SBP_CEF_TEST_EXECUTABLE}" SBP_CEF_TEST_EXECUTABLE)
cmake_path(GET SBP_CEF_TEST_EXECUTABLE FILENAME SBP_CEF_TEST_FILENAME)
if(NOT SBP_CEF_TEST_FILENAME STREQUAL "sbp_cef_adapter_tests.exe")
    message(FATAL_ERROR "Unexpected executable for the CEF adapter test launcher.")
endif()
cmake_path(GET SBP_CEF_TEST_EXECUTABLE PARENT_PATH SBP_CEF_TEST_RUNTIME)

# CMake entrega una raíz corta dentro de la compilación: Cache Storage añade varios
# niveles y puede superar los límites de las rutas de Windows bajo el runtime.
if(NOT DEFINED SBP_CEF_TEST_PROFILE_ROOT OR NOT IS_ABSOLUTE "${SBP_CEF_TEST_PROFILE_ROOT}")
    message(FATAL_ERROR "CEF adapter tests require an absolute isolated profile root from CTest.")
endif()
set(SBP_CEF_TEST_ROOT "${SBP_CEF_TEST_PROFILE_ROOT}")
file(MAKE_DIRECTORY "${SBP_CEF_TEST_ROOT}")
file(REAL_PATH "${SBP_CEF_TEST_ROOT}" SBP_CEF_TEST_ROOT)
string(RANDOM LENGTH 32 ALPHABET 0123456789abcdef SBP_CEF_TEST_ID)
set(SBP_CEF_TEST_NAME "run-${SBP_CEF_TEST_ID}")
set(SBP_CEF_TEST_PROFILE "${SBP_CEF_TEST_ROOT}/${SBP_CEF_TEST_NAME}")
if(EXISTS "${SBP_CEF_TEST_PROFILE}")
    message(FATAL_ERROR "CEF adapter test profile already exists; it will not be reused.")
endif()
file(MAKE_DIRECTORY "${SBP_CEF_TEST_PROFILE}")

# El subproceso recibe la ruta exacta por entorno; sus argumentos no pasan por un shell.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "SBP_CEF_ADAPTER_TEST_PROFILE=${SBP_CEF_TEST_PROFILE}"
        "${SBP_CEF_TEST_EXECUTABLE}"
        --gtest_fail_if_no_test_linked
        --gtest_fail_if_no_test_selected
        "--gtest_output=xml:${SBP_CEF_TEST_RUNTIME}/cef-adapter-results.xml"
    WORKING_DIRECTORY "${SBP_CEF_TEST_RUNTIME}"
    RESULT_VARIABLE SBP_CEF_TEST_RESULT
    TIMEOUT 90
)
if(NOT SBP_CEF_TEST_RESULT STREQUAL "0")
    message(FATAL_ERROR
        "CEF adapter tests failed (${SBP_CEF_TEST_RESULT}). Diagnostics retained in ${SBP_CEF_TEST_PROFILE}"
    )
endif()

# Resuelve y verifica la propiedad del destino antes de cualquier borrado recursivo.
file(REAL_PATH "${SBP_CEF_TEST_PROFILE}" SBP_CEF_RESOLVED_PROFILE)
cmake_path(GET SBP_CEF_RESOLVED_PROFILE PARENT_PATH SBP_CEF_PROFILE_PARENT)
cmake_path(GET SBP_CEF_RESOLVED_PROFILE FILENAME SBP_CEF_PROFILE_NAME)
if(NOT SBP_CEF_PROFILE_PARENT STREQUAL SBP_CEF_TEST_ROOT
    OR NOT SBP_CEF_PROFILE_NAME STREQUAL SBP_CEF_TEST_NAME
    OR IS_SYMLINK "${SBP_CEF_TEST_PROFILE}")
    message(FATAL_ERROR "CEF adapter test profile ownership could not be verified; cleanup skipped.")
endif()

# El proceso ya terminó; reintenta durante unos cinco segundos si Windows tarda en liberar un archivo.
foreach(SBP_CEF_CLEANUP_ATTEMPT RANGE 0 24)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${SBP_CEF_RESOLVED_PROFILE}"
        RESULT_VARIABLE SBP_CEF_CLEANUP_RESULT
        ERROR_VARIABLE SBP_CEF_CLEANUP_ERROR
    )
    if(SBP_CEF_CLEANUP_RESULT STREQUAL "0" AND NOT EXISTS "${SBP_CEF_RESOLVED_PROFILE}")
        message(STATUS "CEF adapter tests passed; isolated profile removed after process exit.")
        return()
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 0.2)
endforeach()
message(FATAL_ERROR
    "CEF adapter tests passed but cleanup failed in ${SBP_CEF_RESOLVED_PROFILE}: ${SBP_CEF_CLEANUP_ERROR}"
)
