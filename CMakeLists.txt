cmake_minimum_required(VERSION 3.10)
project(oxi-memory-pool VERSION 0.1.0 LANGUAGES CXX)

add_library(oxi-memory-pool INTERFACE)

target_compile_features(oxi-memory-pool INTERFACE cxx_std_20)

target_include_directories(oxi-memory-pool INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

include(CTest)
enable_testing()

# -------- pool_basic --------
add_executable(pool_basic_tests
    tests/unit/pool_basic.cpp
)

target_link_libraries(pool_basic_tests
    PRIVATE oxi-memory-pool
)

add_test(
    NAME Pool.Basic
    COMMAND pool_basic_tests
)

# -------- error_callback --------
add_executable(error_callback_tests
    tests/unit/error_callback.cpp
)

target_link_libraries(error_callback_tests
    PRIVATE oxi-memory-pool
)

add_test(
    NAME Pool.ErrorCallback
    COMMAND error_callback_tests
)

# -------- lifetime --------
add_executable(lifetime_tests
    tests/unit/lifetime.cpp
)

target_link_libraries(lifetime_tests
    PRIVATE oxi-memory-pool
)

add_test(
    NAME Pool.Lifetime
    COMMAND lifetime_tests
)

# -------- handle_move --------
add_executable(handle_move_tests
    tests/unit/handle_move.cpp
)

target_link_libraries(handle_move_tests
    PRIVATE oxi-memory-pool
)

add_test(
    NAME Pool.HandleMove
    COMMAND handle_move_tests
)

# -------- reuse --------
add_executable(reuse_tests
    tests/unit/reuse.cpp
)

target_link_libraries(reuse_tests
    PRIVATE oxi-memory-pool
)

add_test(
    NAME Pool.Reuse
    COMMAND reuse_tests
)

# -------- thread_safety --------
add_executable(thread_safety_tests
    tests/unit/thread_safety.cpp
)

target_link_libraries(thread_safety_tests
    PRIVATE oxi-memory-pool
)

add_test(
    NAME Pool.ThreadSafety
    COMMAND thread_safety_tests
)

# -------- exception_safety --------
add_executable(exception_safety_tests
    tests/unit/exception_safety.cpp
)

target_link_libraries(exception_safety_tests
    PRIVATE oxi-memory-pool
)

add_test(
    NAME Pool.ExceptionSafety
    COMMAND exception_safety_tests
)

# -------- thread_stress --------
add_executable(thread_stress_tests
    tests/unit/thread_stress.cpp
)

target_link_libraries(thread_stress_tests
    PRIVATE oxi-memory-pool
)

add_test(
    NAME Pool.ThreadStress
    COMMAND thread_stress_tests
)