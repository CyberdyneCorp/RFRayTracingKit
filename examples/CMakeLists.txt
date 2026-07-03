add_executable(rftrace_simple_los simple_los/main.cpp)
target_link_libraries(rftrace_simple_los PRIVATE rftrace::rftrace)

add_executable(rftrace_city_reflection city_reflection/main.cpp)
target_link_libraries(rftrace_city_reflection PRIVATE rftrace::rftrace)

add_executable(rftrace_coverage_grid coverage_grid/main.cpp)
target_link_libraries(rftrace_coverage_grid PRIVATE rftrace::rftrace)

add_executable(rftrace_advanced_rf advanced_rf/main.cpp)
target_link_libraries(rftrace_advanced_rf PRIVATE rftrace::rftrace)

# CPU-vs-CUDA traversal benchmark. Runs CPU-only unless the library was built
# with -DRFTRACE_ENABLE_CUDA=ON and an NVIDIA GPU + OptiX are present at runtime.
add_executable(rftrace_cuda_benchmark cuda_benchmark/main.cpp)
target_link_libraries(rftrace_cuda_benchmark PRIVATE rftrace::rftrace)
