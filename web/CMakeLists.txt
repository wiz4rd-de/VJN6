# Web build (SPEC §15.2): vjn6_core + web/worklet.cpp compiled with emcc into ui/public/vjn6.js (loader,
# also loaded into the AudioWorkletGlobalScope) and ui/public/vjn6.wasm. Emscripten only; added by the
# root CMakeLists.txt when VJN6_BUILD_WEB=ON under emcmake. web/build.sh is the documented entry point.
#
# The core is compiled a second time here (vjn6_core_web) because the web module needs shared memory
# (-sWASM_WORKERS: atomics, bulk memory), which must not leak into the Node test/benchmark builds.
# -pthread is added to the §15.2 flags because vjn6_core uses std::mutex (snapshot readers) and
# std::this_thread::sleep_for (Instrument::loadState); Emscripten's Wasm-Workers-only libc/libc++ do not
# provide pthread_mutex_* or emscripten_thread_sleep. No pthread is ever started (PTHREAD_POOL_SIZE 0).

get_target_property(VJN6_CORE_DIR vjn6_core SOURCE_DIR)
get_target_property(VJN6_CORE_SRCS vjn6_core SOURCES)
list(TRANSFORM VJN6_CORE_SRCS PREPEND "${VJN6_CORE_DIR}/")

set(VJN6_WEB_FLAGS -O3 -msimd128 -sWASM_WORKERS -pthread)

add_library(vjn6_core_web STATIC ${VJN6_CORE_SRCS})
target_include_directories(vjn6_core_web PUBLIC ${VJN6_CORE_DIR}/include PRIVATE ${VJN6_CORE_DIR})
target_compile_features(vjn6_core_web PUBLIC cxx_std_17)
target_compile_options(vjn6_core_web PRIVATE ${VJN6_WEB_FLAGS} -Wall -Wextra -Wno-unused-parameter)
vjn6_determinism(vjn6_core_web)

set(VJN6_WEB_EXPORTS
  _vjn6_init _vjn6_load_state _vjn6_save_state _vjn6_state_ptr _vjn6_command _vjn6_poll _vjn6_load_file
  _vjn6_start_audio _vjn6_audio_context _vjn6_audio_node _malloc _free)
set(VJN6_WEB_RUNTIME
  ccall cwrap UTF8ToString stringToUTF8 lengthBytesUTF8 stringToNewUTF8 HEAPU8 emscriptenGetAudioObject)
list(JOIN VJN6_WEB_EXPORTS "," VJN6_WEB_EXPORTS_CSV)
list(JOIN VJN6_WEB_RUNTIME "," VJN6_WEB_RUNTIME_CSV)

add_executable(vjn6_web worklet.cpp)
target_link_libraries(vjn6_web PRIVATE vjn6_core_web)
target_compile_options(vjn6_web PRIVATE ${VJN6_WEB_FLAGS} -Wall -Wextra)
vjn6_determinism(vjn6_web)
target_link_options(vjn6_web PRIVATE
  ${VJN6_WEB_FLAGS}
  -sAUDIO_WORKLET
  "-sENVIRONMENT=web,worker,worklet"
  -sMODULARIZE=1
  -sEXPORT_NAME=createVjn6
  "-sEXPORTED_FUNCTIONS=${VJN6_WEB_EXPORTS_CSV}"
  "-sEXPORTED_RUNTIME_METHODS=${VJN6_WEB_RUNTIME_CSV}"
  -sINITIAL_MEMORY=64MB
  -sALLOW_MEMORY_GROWTH=1
  -Wno-pthreads-mem-growth
  -sMAXIMUM_MEMORY=512MB
  -sSTACK_SIZE=4MB
  -sFILESYSTEM=0)
set_target_properties(vjn6_web PROPERTIES
  OUTPUT_NAME vjn6
  SUFFIX ".js"
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/ui/public")
