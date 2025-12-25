# CMake generated Testfile for 
# Source directory: /home/oximer/VSCodeProjects/OxiMemoryPool
# Build directory: /home/oximer/VSCodeProjects/OxiMemoryPool/out/build/Configure preset using toolchain file
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(Pool.Basic "/home/oximer/VSCodeProjects/OxiMemoryPool/out/build/Configure preset using toolchain file/pool_basic_tests")
set_tests_properties(Pool.Basic PROPERTIES  _BACKTRACE_TRIPLES "/home/oximer/VSCodeProjects/OxiMemoryPool/CMakeLists.txt;24;add_test;/home/oximer/VSCodeProjects/OxiMemoryPool/CMakeLists.txt;0;")
add_test(Pool.ErrorCallback "/home/oximer/VSCodeProjects/OxiMemoryPool/out/build/Configure preset using toolchain file/error_callback_tests")
set_tests_properties(Pool.ErrorCallback PROPERTIES  _BACKTRACE_TRIPLES "/home/oximer/VSCodeProjects/OxiMemoryPool/CMakeLists.txt;38;add_test;/home/oximer/VSCodeProjects/OxiMemoryPool/CMakeLists.txt;0;")
add_test(Pool.Lifetime "/home/oximer/VSCodeProjects/OxiMemoryPool/out/build/Configure preset using toolchain file/lifetime_tests")
set_tests_properties(Pool.Lifetime PROPERTIES  _BACKTRACE_TRIPLES "/home/oximer/VSCodeProjects/OxiMemoryPool/CMakeLists.txt;52;add_test;/home/oximer/VSCodeProjects/OxiMemoryPool/CMakeLists.txt;0;")
add_test(Pool.HandleMove "/home/oximer/VSCodeProjects/OxiMemoryPool/out/build/Configure preset using toolchain file/handle_move_tests")
set_tests_properties(Pool.HandleMove PROPERTIES  _BACKTRACE_TRIPLES "/home/oximer/VSCodeProjects/OxiMemoryPool/CMakeLists.txt;66;add_test;/home/oximer/VSCodeProjects/OxiMemoryPool/CMakeLists.txt;0;")
add_test(Pool.Reuse "/home/oximer/VSCodeProjects/OxiMemoryPool/out/build/Configure preset using toolchain file/reuse_tests")
set_tests_properties(Pool.Reuse PROPERTIES  _BACKTRACE_TRIPLES "/home/oximer/VSCodeProjects/OxiMemoryPool/CMakeLists.txt;80;add_test;/home/oximer/VSCodeProjects/OxiMemoryPool/CMakeLists.txt;0;")
add_test(Pool.ThreadSafety "/home/oximer/VSCodeProjects/OxiMemoryPool/out/build/Configure preset using toolchain file/thread_safety_tests")
set_tests_properties(Pool.ThreadSafety PROPERTIES  _BACKTRACE_TRIPLES "/home/oximer/VSCodeProjects/OxiMemoryPool/CMakeLists.txt;94;add_test;/home/oximer/VSCodeProjects/OxiMemoryPool/CMakeLists.txt;0;")
add_test(Pool.ExceptionSafety "/home/oximer/VSCodeProjects/OxiMemoryPool/out/build/Configure preset using toolchain file/exception_safety_tests")
set_tests_properties(Pool.ExceptionSafety PROPERTIES  _BACKTRACE_TRIPLES "/home/oximer/VSCodeProjects/OxiMemoryPool/CMakeLists.txt;108;add_test;/home/oximer/VSCodeProjects/OxiMemoryPool/CMakeLists.txt;0;")
add_test(Pool.ThreadStress "/home/oximer/VSCodeProjects/OxiMemoryPool/out/build/Configure preset using toolchain file/thread_stress_tests")
set_tests_properties(Pool.ThreadStress PROPERTIES  _BACKTRACE_TRIPLES "/home/oximer/VSCodeProjects/OxiMemoryPool/CMakeLists.txt;122;add_test;/home/oximer/VSCodeProjects/OxiMemoryPool/CMakeLists.txt;0;")
