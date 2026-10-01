find_program(GLSLANG_VALIDATOR glslangValidator REQUIRED)

set(ELEMENTWISE_ADD_SHADER "${CMAKE_CURRENT_SOURCE_DIR}/elementwise_add.comp")
set(ELEMENTWISE_ADD_SPV "${CMAKE_CURRENT_BINARY_DIR}/elementwise_add.comp.spv")

add_custom_command(
    OUTPUT "${ELEMENTWISE_ADD_SPV}"
    COMMAND "${GLSLANG_VALIDATOR}" -V "${ELEMENTWISE_ADD_SHADER}" -o "${ELEMENTWISE_ADD_SPV}"
    DEPENDS "${ELEMENTWISE_ADD_SHADER}"
    VERBATIM
)

add_executable(vucol_elementwise_add
    elementwise_add.cpp
    "${ELEMENTWISE_ADD_SPV}"
)
target_link_libraries(vucol_elementwise_add PRIVATE vucol)
target_compile_definitions(vucol_elementwise_add
    PRIVATE VUCOL_ELEMENTWISE_ADD_SPV="${ELEMENTWISE_ADD_SPV}"
)
