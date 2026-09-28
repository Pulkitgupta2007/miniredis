add_test([=[StoreTest.BasicSetAndGet]=]  /Users/pulkit/miniredis/server/build/store_test [==[--gtest_filter=StoreTest.BasicSetAndGet]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[StoreTest.BasicSetAndGet]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[/Users/pulkit/miniredis/server/tests/store_test.cpp:23]==]
    WORKING_DIRECTORY [==[/Users/pulkit/miniredis/server/build]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[StoreTest.OverwriteValue]=]  /Users/pulkit/miniredis/server/build/store_test [==[--gtest_filter=StoreTest.OverwriteValue]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[StoreTest.OverwriteValue]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[/Users/pulkit/miniredis/server/tests/store_test.cpp:35]==]
    WORKING_DIRECTORY [==[/Users/pulkit/miniredis/server/build]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[StoreTest.SetWithConditions]=]  /Users/pulkit/miniredis/server/build/store_test [==[--gtest_filter=StoreTest.SetWithConditions]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[StoreTest.SetWithConditions]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[/Users/pulkit/miniredis/server/tests/store_test.cpp:45]==]
    WORKING_DIRECTORY [==[/Users/pulkit/miniredis/server/build]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[StoreTest.DelSingleAndMultiple]=]  /Users/pulkit/miniredis/server/build/store_test [==[--gtest_filter=StoreTest.DelSingleAndMultiple]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[StoreTest.DelSingleAndMultiple]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[/Users/pulkit/miniredis/server/tests/store_test.cpp:62]==]
    WORKING_DIRECTORY [==[/Users/pulkit/miniredis/server/build]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[StoreTest.ExistsSingleAndMultiple]=]  /Users/pulkit/miniredis/server/build/store_test [==[--gtest_filter=StoreTest.ExistsSingleAndMultiple]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[StoreTest.ExistsSingleAndMultiple]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[/Users/pulkit/miniredis/server/tests/store_test.cpp:80]==]
    WORKING_DIRECTORY [==[/Users/pulkit/miniredis/server/build]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[StoreTest.TTLAndPTTLCalculations]=]  /Users/pulkit/miniredis/server/build/store_test [==[--gtest_filter=StoreTest.TTLAndPTTLCalculations]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[StoreTest.TTLAndPTTLCalculations]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[/Users/pulkit/miniredis/server/tests/store_test.cpp:92]==]
    WORKING_DIRECTORY [==[/Users/pulkit/miniredis/server/build]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[StoreTest.PassiveExpiration]=]  /Users/pulkit/miniredis/server/build/store_test [==[--gtest_filter=StoreTest.PassiveExpiration]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[StoreTest.PassiveExpiration]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[/Users/pulkit/miniredis/server/tests/store_test.cpp:115]==]
    WORKING_DIRECTORY [==[/Users/pulkit/miniredis/server/build]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[StoreTest.ExpireAndPexpire]=]  /Users/pulkit/miniredis/server/build/store_test [==[--gtest_filter=StoreTest.ExpireAndPexpire]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[StoreTest.ExpireAndPexpire]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[/Users/pulkit/miniredis/server/tests/store_test.cpp:130]==]
    WORKING_DIRECTORY [==[/Users/pulkit/miniredis/server/build]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[StoreTest.PersistClearsTTL]=]  /Users/pulkit/miniredis/server/build/store_test [==[--gtest_filter=StoreTest.PersistClearsTTL]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[StoreTest.PersistClearsTTL]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[/Users/pulkit/miniredis/server/tests/store_test.cpp:148]==]
    WORKING_DIRECTORY [==[/Users/pulkit/miniredis/server/build]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[StoreTest.OverwriteClearsPriorTTL]=]  /Users/pulkit/miniredis/server/build/store_test [==[--gtest_filter=StoreTest.OverwriteClearsPriorTTL]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[StoreTest.OverwriteClearsPriorTTL]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[/Users/pulkit/miniredis/server/tests/store_test.cpp:167]==]
    WORKING_DIRECTORY [==[/Users/pulkit/miniredis/server/build]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[StoreTest.ZeroOrNegativeTTLDeletesKey]=]  /Users/pulkit/miniredis/server/build/store_test [==[--gtest_filter=StoreTest.ZeroOrNegativeTTLDeletesKey]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[StoreTest.ZeroOrNegativeTTLDeletesKey]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[/Users/pulkit/miniredis/server/tests/store_test.cpp:182]==]
    WORKING_DIRECTORY [==[/Users/pulkit/miniredis/server/build]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[StoreTest.ActiveExpireCycle]=]  /Users/pulkit/miniredis/server/build/store_test [==[--gtest_filter=StoreTest.ActiveExpireCycle]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[StoreTest.ActiveExpireCycle]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[/Users/pulkit/miniredis/server/tests/store_test.cpp:195]==]
    WORKING_DIRECTORY [==[/Users/pulkit/miniredis/server/build]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[StoreTest.DBSizeAndFlushDB]=]  /Users/pulkit/miniredis/server/build/store_test [==[--gtest_filter=StoreTest.DBSizeAndFlushDB]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[StoreTest.DBSizeAndFlushDB]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[/Users/pulkit/miniredis/server/tests/store_test.cpp:228]==]
    WORKING_DIRECTORY [==[/Users/pulkit/miniredis/server/build]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[StoreTest.KeysPatternMatching]=]  /Users/pulkit/miniredis/server/build/store_test [==[--gtest_filter=StoreTest.KeysPatternMatching]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[StoreTest.KeysPatternMatching]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[/Users/pulkit/miniredis/server/tests/store_test.cpp:242]==]
    WORKING_DIRECTORY [==[/Users/pulkit/miniredis/server/build]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
set(store_test_TESTS [==[StoreTest.BasicSetAndGet]==] [==[StoreTest.OverwriteValue]==] [==[StoreTest.SetWithConditions]==] [==[StoreTest.DelSingleAndMultiple]==] [==[StoreTest.ExistsSingleAndMultiple]==] [==[StoreTest.TTLAndPTTLCalculations]==] [==[StoreTest.PassiveExpiration]==] [==[StoreTest.ExpireAndPexpire]==] [==[StoreTest.PersistClearsTTL]==] [==[StoreTest.OverwriteClearsPriorTTL]==] [==[StoreTest.ZeroOrNegativeTTLDeletesKey]==] [==[StoreTest.ActiveExpireCycle]==] [==[StoreTest.DBSizeAndFlushDB]==] [==[StoreTest.KeysPatternMatching]==])
