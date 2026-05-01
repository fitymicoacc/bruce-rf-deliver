# Tests use Unity (https://github.com/ThrowTheSwitch/Unity). We vendor a
# minimal copy via FetchContent so the build has no system dependency.

include(FetchContent)

FetchContent_Declare(
    unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG v2.6.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(unity)

function(lil_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE lil::lil unity)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

lil_add_test(test_wire       test_wire.c)
lil_add_test(test_protocols  test_protocols.c)
lil_add_test(test_decoder    test_decoder.c)
