cmake_minimum_required(VERSION 3.20)
project(stm32_ssh_uart_gateway VERSION 0.1.0 LANGUAGES C)

add_library(gateway STATIC src/gateway.c)
target_include_directories(gateway PUBLIC include)
set_target_properties(gateway PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

add_executable(gateway_tests tests/test_gateway.c)
target_link_libraries(gateway_tests PRIVATE gateway)
add_executable(host_demo examples/host_demo.c)
target_link_libraries(host_demo PRIVATE gateway)

foreach(target gateway gateway_tests host_demo)
    set_target_properties(${target} PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endforeach()

enable_testing()
add_test(NAME gateway_core COMMAND gateway_tests)
add_test(NAME host_demonstration COMMAND host_demo)
