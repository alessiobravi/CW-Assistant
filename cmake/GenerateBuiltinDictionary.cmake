# Compiles a dictionary file into the library as a last-resort fallback.
#
# The text file stays the single source of truth and the operator's copy still
# wins at runtime. This exists because the decoder cannot work at all without
# an alphabet: when loading one failed in a packaged build, the application
# tracked signals and decoded nothing, with no diagnostic. A generated header
# removes that failure class without reintroducing a table that has to be kept
# in step by hand, because it is produced from the same file at build time.
function(cwa_generate_builtin_dictionary source_path output_path symbol)
  file(READ "${source_path}" contents)
  if(contents MATCHES "\\)CWADICT\"")
    message(FATAL_ERROR
      "${source_path} contains the raw string delimiter used to embed it")
  endif()
  set(generated
"// Generated from ${source_path} at build time. Do not edit.\n\
#pragma once\n\
\n\
namespace cwassistant::core {\n\
\n\
inline constexpr const char* ${symbol} = R\"CWADICT(${contents})CWADICT\";\n\
\n\
}  // namespace cwassistant::core\n")
  file(GENERATE OUTPUT "${output_path}" CONTENT "${generated}")
endfunction()
