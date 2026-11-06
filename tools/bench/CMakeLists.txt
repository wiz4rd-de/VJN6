# Render benchmark (SPEC §9.1, AC-N1.1): native, and under emcmake as vjn6_bench.js for Node.
add_executable(vjn6_bench main.cpp)
target_link_libraries(vjn6_bench PRIVATE vjn6_core)
vjn6_determinism(vjn6_bench)
