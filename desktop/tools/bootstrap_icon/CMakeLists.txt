enable_language(RC)
add_executable(sbp_bootstrap_icon main.cpp icon.rc)
target_compile_definitions(sbp_bootstrap_icon PRIVATE UNICODE _UNICODE)
set(icon_assets_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../../assets")
target_include_directories(sbp_bootstrap_icon PRIVATE "${icon_assets_dir}")

# Se trabaja sobre una copia privada. La salida se publica solo tras actualizar los recursos.
set(bootstrap_dir "${CMAKE_CURRENT_BINARY_DIR}/branded/$<CONFIG>")
set(bootstrap_exe "${bootstrap_dir}/SecureBrowser.exe")
add_custom_command(
    OUTPUT "${bootstrap_exe}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${bootstrap_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CEF_ROOT}/$<CONFIG>/bootstrap.exe" "${bootstrap_exe}.tmp"
    COMMAND sbp_bootstrap_icon "${bootstrap_exe}.tmp"
    COMMAND "${CMAKE_COMMAND}" -E rename "${bootstrap_exe}.tmp" "${bootstrap_exe}"
    DEPENDS sbp_bootstrap_icon "${CEF_ROOT}/$<CONFIG>/bootstrap.exe"
    COMMENT "Embedding the SecureBrowser icon in the CEF bootstrap copy"
    VERBATIM
)
# Esta dependencia repone también el SVG si falta, incluso cuando la DLL ya está compilada.
add_custom_target(sbp_deploy_bootstrap
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:SecureBrowser>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${bootstrap_exe}" "${icon_assets_dir}/securebrowser-desktop.svg" "$<TARGET_FILE_DIR:SecureBrowser>"
    DEPENDS "${bootstrap_exe}"
    VERBATIM
)
