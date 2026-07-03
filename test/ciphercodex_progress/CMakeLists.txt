add_executable(CipherCodexProgressTest
  CipherCodexProgressTest.cpp
)

target_include_directories(CipherCodexProgressTest PRIVATE
  ${REPO_ROOT}/lib/KOReaderSync
)

target_link_libraries(CipherCodexProgressTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(CipherCodexProgressTest)
