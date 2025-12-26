file(GLOB COMMAND_SOURCES "${CMAKE_SOURCE_DIR}/src/commands/*Command.cpp")

set(GENERATED_FILE "${CMAKE_BINARY_DIR}/command_link_anchor.cpp")
file(WRITE ${GENERATED_FILE} "// Auto-generated file to force link commands\n")

file(APPEND ${GENERATED_FILE} "extern \"C\" void force_link_all_commands() {\n")

foreach(cmd_file ${COMMAND_SOURCES})
    get_filename_component(cmd ${cmd_file} NAME_WE)
    string(REGEX REPLACE "Command$" "" cmd_base ${cmd})
    set(func_name "force_link_${cmd}")
    file(APPEND ${GENERATED_FILE} "  extern void ${func_name}(); ${func_name}();\n")
endforeach()

file(APPEND ${GENERATED_FILE} "}\n")

list(APPEND MAIN_SOURCES "${GENERATED_FILE}")
