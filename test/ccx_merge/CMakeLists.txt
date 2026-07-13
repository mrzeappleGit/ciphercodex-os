add_executable(ccx_merge_test CcxMergeTest.cpp)

target_link_libraries(ccx_merge_test PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ccx_merge_test)
