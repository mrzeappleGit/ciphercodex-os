add_executable(CcxSnapshotParseTest
  CcxSnapshotParseTest.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)

target_include_directories(CcxSnapshotParseTest PRIVATE
  ${REPO_ROOT}/lib/JsonParser
)

target_link_libraries(CcxSnapshotParseTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(CcxSnapshotParseTest)
