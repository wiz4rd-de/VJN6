# Bit-identical rendering on every platform and in WASM (SPEC §9.4): no FP contraction, no fast-math.
function(vjn6_determinism target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /fp:precise /fp:contract-)
  else()
    target_compile_options(${target} PRIVATE -ffp-contract=off -fno-fast-math)
  endif()
endfunction()
