add_executable(disktree_tests
    unit/test_humanize.cpp
)

target_link_libraries(disktree_tests
    PRIVATE
        disktree_core
        Catch2::Catch2WithMain
)

# Auto-discover TEST_CASEs and register with ctest
include(Catch)
catch_discover_tests(disktree_tests)