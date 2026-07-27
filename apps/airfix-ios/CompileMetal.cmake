cmake_minimum_required(VERSION 3.25)

foreach(required IN ITEMS
    AIRFIX_XCRUN_EXECUTABLE
    AIRFIX_METAL_SDK
    AIRFIX_METAL_SOURCE
    AIRFIX_METAL_WORK_BASE)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "missing required Metal build input: ${required}")
    endif()
endforeach()

if("$ENV{CONFIGURATION}" STREQUAL "")
    message(FATAL_ERROR "Xcode did not provide CONFIGURATION")
endif()
if("$ENV{TARGET_BUILD_DIR}" STREQUAL "")
    message(FATAL_ERROR "Xcode did not provide TARGET_BUILD_DIR")
endif()
if("$ENV{WRAPPER_NAME}" STREQUAL "")
    message(FATAL_ERROR "Xcode did not provide WRAPPER_NAME")
endif()

set(work_dir "${AIRFIX_METAL_WORK_BASE}/$ENV{CONFIGURATION}")
set(air_file "${work_dir}/AirfixShaders.air")
set(bundle_dir "$ENV{TARGET_BUILD_DIR}/$ENV{WRAPPER_NAME}")
set(library_file "${bundle_dir}/default.metallib")
file(MAKE_DIRECTORY "${work_dir}")

execute_process(
    COMMAND "${AIRFIX_XCRUN_EXECUTABLE}" -sdk "${AIRFIX_METAL_SDK}"
        metal -c "${AIRFIX_METAL_SOURCE}" -o "${air_file}"
    RESULT_VARIABLE compile_result
    OUTPUT_VARIABLE compile_output
    ERROR_VARIABLE compile_error)
if(NOT compile_result EQUAL 0)
    message(FATAL_ERROR
        "Metal source compilation failed (${compile_result})\n"
        "${compile_output}${compile_error}")
endif()

execute_process(
    COMMAND "${AIRFIX_XCRUN_EXECUTABLE}" -sdk "${AIRFIX_METAL_SDK}"
        metallib "${air_file}" -o "${library_file}"
    RESULT_VARIABLE link_result
    OUTPUT_VARIABLE link_output
    ERROR_VARIABLE link_error)
if(NOT link_result EQUAL 0)
    message(FATAL_ERROR
        "Metal library link failed (${link_result})\n"
        "${link_output}${link_error}")
endif()

if(NOT EXISTS "${library_file}")
    message(FATAL_ERROR "Metal compiler did not create ${library_file}")
endif()
