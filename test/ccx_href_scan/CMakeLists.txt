add_executable(ccx_href_scan_test CcxHrefScanTest.cpp)

target_link_libraries(ccx_href_scan_test PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ccx_href_scan_test)
