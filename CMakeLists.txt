cmake_minimum_required(VERSION 3.10)
project(http_server)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 OpenSSL 和 CURL
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

# 添加头文件路径
include_directories(
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/HttpServer/include
    ${PROJECT_SOURCE_DIR}/AIApps/ChatServer/include
    /usr/include/mysql-cppconn-8
    /usr/include/mysql
    ${OPENSSL_INCLUDE_DIR}
    ${CURL_INCLUDE_DIRS}
    /usr/include/opencv4          # OpenCV 头文件路径
    /usr/local/include            # SimpleAmqpClient & ONNX Runtime
)

# 查找必要的库
find_library(MYSQLCPPCONN_LIBRARY
    NAMES mysqlcppconn mysqlcppconn8
    PATHS /usr/lib /usr/lib64 /usr/local/lib
)

find_library(ONNXRUNTIME_LIBRARY
    NAMES onnxruntime
    PATHS /usr/lib /usr/lib64 /usr/local/lib
)

# 添加源文件
file(GLOB_RECURSE HTTP_SERVER_SRC
    "${PROJECT_SOURCE_DIR}/HttpServer/src/*.cpp"
)

file(GLOB_RECURSE GOMOKU_SERVER_SRC
    "${PROJECT_SOURCE_DIR}/AIApps/ChatServer/src/*.cpp"
)

# 主程序文件
set(MAIN_SRC "${PROJECT_SOURCE_DIR}/AIApps/ChatServer/src/main.cpp")

# 添加可执行文件
add_executable(http_server
    ${MAIN_SRC}
    ${HTTP_SERVER_SRC}
    ${GOMOKU_SERVER_SRC}
)

# 链接库
enable_testing()

add_executable(http_stream_writer_test
    ${PROJECT_SOURCE_DIR}/tests/http_stream_writer_test.cpp
)

add_test(NAME http_stream_writer_test COMMAND http_stream_writer_test)

add_executable(login_session_policy_test
    ${PROJECT_SOURCE_DIR}/tests/login_session_policy_test.cpp
)

add_test(NAME login_session_policy_test COMMAND login_session_policy_test)

add_executable(rag_core_test
    ${PROJECT_SOURCE_DIR}/tests/rag_core_test.cpp
    ${PROJECT_SOURCE_DIR}/AIApps/ChatServer/src/rag/EmbeddingClient.cpp
    ${PROJECT_SOURCE_DIR}/AIApps/ChatServer/src/rag/TextSplitter.cpp
    ${PROJECT_SOURCE_DIR}/AIApps/ChatServer/src/rag/HashEmbeddingClient.cpp
    ${PROJECT_SOURCE_DIR}/AIApps/ChatServer/src/rag/JsonVectorStore.cpp
    ${PROJECT_SOURCE_DIR}/AIApps/ChatServer/src/rag/FitnessRagService.cpp
)

target_link_libraries(rag_core_test ${CURL_LIBRARIES})

add_test(NAME rag_core_test COMMAND rag_core_test)

add_executable(rag_vector_store_test
    ${PROJECT_SOURCE_DIR}/tests/rag_vector_store_test.cpp
    ${PROJECT_SOURCE_DIR}/AIApps/ChatServer/src/rag/EmbeddingClient.cpp
    ${PROJECT_SOURCE_DIR}/AIApps/ChatServer/src/rag/TextSplitter.cpp
    ${PROJECT_SOURCE_DIR}/AIApps/ChatServer/src/rag/HashEmbeddingClient.cpp
    ${PROJECT_SOURCE_DIR}/AIApps/ChatServer/src/rag/JsonVectorStore.cpp
    ${PROJECT_SOURCE_DIR}/AIApps/ChatServer/src/rag/VectorRagService.cpp
)

target_link_libraries(rag_vector_store_test ${CURL_LIBRARIES})

add_test(NAME rag_vector_store_test COMMAND rag_vector_store_test)

add_executable(fitness_tools_test
    ${PROJECT_SOURCE_DIR}/tests/fitness_tools_test.cpp
    ${PROJECT_SOURCE_DIR}/AIApps/ChatServer/src/tools/FitnessToolService.cpp
)

add_test(NAME fitness_tools_test COMMAND fitness_tools_test)

add_executable(agent_tool_calling_test
    ${PROJECT_SOURCE_DIR}/tests/agent_tool_calling_test.cpp
    ${PROJECT_SOURCE_DIR}/AIApps/ChatServer/src/tools/AgentToolCalling.cpp
    ${PROJECT_SOURCE_DIR}/AIApps/ChatServer/src/tools/FitnessToolService.cpp
)

add_test(NAME agent_tool_calling_test COMMAND agent_tool_calling_test)

add_executable(ai_quota_service_test
    ${PROJECT_SOURCE_DIR}/tests/ai_quota_service_test.cpp
)

add_test(NAME ai_quota_service_test COMMAND ai_quota_service_test)

add_executable(password_hasher_test
    ${PROJECT_SOURCE_DIR}/tests/password_hasher_test.cpp
    ${PROJECT_SOURCE_DIR}/AIApps/ChatServer/src/auth/PasswordHasher.cpp
)

target_link_libraries(password_hasher_test ssl crypto)

add_test(NAME password_hasher_test COMMAND password_hasher_test)

add_executable(redis_session_storage_test
    ${PROJECT_SOURCE_DIR}/tests/redis_session_storage_test.cpp
    ${PROJECT_SOURCE_DIR}/HttpServer/src/session/RedisSessionStorage.cpp
    ${PROJECT_SOURCE_DIR}/HttpServer/src/session/Session.cpp
)

target_link_libraries(redis_session_storage_test muduo_base)

add_test(NAME redis_session_storage_test COMMAND redis_session_storage_test)

add_executable(rate_limit_middleware_test
    ${PROJECT_SOURCE_DIR}/tests/rate_limit_middleware_test.cpp
    ${PROJECT_SOURCE_DIR}/HttpServer/src/middleware/ratelimit/RateLimitMiddleware.cpp
    ${PROJECT_SOURCE_DIR}/HttpServer/src/http/HttpRequest.cpp
    ${PROJECT_SOURCE_DIR}/HttpServer/src/http/HttpResponse.cpp
)

target_link_libraries(rate_limit_middleware_test muduo_base)

add_test(NAME rate_limit_middleware_test COMMAND rate_limit_middleware_test)

add_executable(exercise_library_policy_test
    ${PROJECT_SOURCE_DIR}/tests/exercise_library_policy_test.cpp
)

add_test(NAME exercise_library_policy_test COMMAND exercise_library_policy_test)

target_link_libraries(http_server
    pthread
    muduo_net
    muduo_base
    mysqlcppconn
    mysqlclient
    ssl
    crypto
    ${CURL_LIBRARIES}
    ${OPENCV4_LIBRARIES}
    /usr/lib/x86_64-linux-gnu/libopencv_core.so
    /usr/lib/x86_64-linux-gnu/libopencv_imgproc.so
    /usr/lib/x86_64-linux-gnu/libopencv_highgui.so
    /usr/lib/x86_64-linux-gnu/libopencv_imgcodecs.so
    /usr/lib/x86_64-linux-gnu/libopencv_videoio.so
    /usr/lib/x86_64-linux-gnu/libopencv_dnn.so

    ${ONNXRUNTIME_LIBRARY}        # 自动查找的 ONNX Runtime 库

    SimpleAmqpClient
    rabbitmq
)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    target_link_libraries(http_server stdc++fs)
    target_link_libraries(rag_core_test stdc++fs)
    target_link_libraries(rag_vector_store_test stdc++fs)
endif()

# 如果库不在默认路径，添加链接目录
link_directories(/usr/local/lib)

# 打印调试信息
message(STATUS "Include directories:")
get_property(dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY INCLUDE_DIRECTORIES)
foreach(dir ${dirs})
    message(STATUS "  ${dir}")
endforeach()

message(STATUS "ONNX Runtime library: ${ONNXRUNTIME_LIBRARY}")
message(STATUS "Main source file: ${MAIN_SRC}")
message(STATUS "HTTP Server sources: ${HTTP_SERVER_SRC}")
message(STATUS "Gomoku Server sources: ${GOMOKU_SERVER_SRC}")
