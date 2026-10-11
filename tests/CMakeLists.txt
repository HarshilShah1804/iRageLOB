# Module 1 – Fixed-Point
add_executable(test_fixed_point test_fixed_point.cpp)
target_include_directories(test_fixed_point PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME fixed_point COMMAND test_fixed_point)

# Module 2 – Order Book
add_executable(test_order_book test_order_book.cpp)
target_include_directories(test_order_book PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME order_book COMMAND test_order_book)

# Module 3 – Event Parser (needs RapidJSON)
add_executable(test_event_parser test_event_parser.cpp
    ${CMAKE_SOURCE_DIR}/src/event_parser.cpp)
target_include_directories(test_event_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/rapidjson/include)
add_test(NAME event_parser COMMAND test_event_parser)

# Module 4 – CSV Writer
add_executable(test_csv_writer test_csv_writer.cpp)
target_include_directories(test_csv_writer PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME csv_writer COMMAND test_csv_writer)

# Module 5 – Replay Engine
add_executable(test_replay_engine test_replay_engine.cpp
    ${CMAKE_SOURCE_DIR}/src/replay_engine.cpp
    ${CMAKE_SOURCE_DIR}/src/event_parser.cpp)
target_include_directories(test_replay_engine PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/rapidjson/include)
add_test(NAME replay_engine COMMAND test_replay_engine)
