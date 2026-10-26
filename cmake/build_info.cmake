# Writes build_info.h with the build time (UTC) and the git commit. Run on every build by the
# revelations_build_info target, the file is only rewritten when its content changes.
# Inputs: OUT (header path), SRC (source folder).
string(TIMESTAMP stamp "%Y-%m-%dT%H:%M:%SZ" UTC)
set(commit "")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" describe --always --dirty --abbrev=7
                    WORKING_DIRECTORY "${SRC}" OUTPUT_VARIABLE commit
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
set(content "#pragma once\n#define REVELATIONS_BUILD_TIMESTAMP \"${stamp}\"\n#define REVELATIONS_GIT_COMMIT \"${commit}\"\n")
if(EXISTS "${OUT}")
    file(READ "${OUT}" old)
endif()
if(NOT old STREQUAL content)
    file(WRITE "${OUT}" "${content}")
endif()
