cmake_minimum_required(VERSION 3.16)
project(mimiclaw LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)

find_path(CJSON_INCLUDE_DIR
    NAMES cJSON.h
    PATH_SUFFIXES cjson
)
find_library(CJSON_LIBRARY
    NAMES cjson libcjson
)
if(NOT CJSON_INCLUDE_DIR OR NOT CJSON_LIBRARY)
    message(FATAL_ERROR "cJSON headers/library not found. Install libcjson-dev.")
endif()

find_path(LIBWEBSOCKETS_INCLUDE_DIR NAMES libwebsockets.h)
find_library(LIBWEBSOCKETS_LIBRARY NAMES websockets libwebsockets)

set(MIMICLAW_SOURCES
    main/agent/agent_loop.c
    main/agent/agent_trace.c
    main/agent/context_builder.c
    main/bus/message_bus.c
    main/cli/serial_cli.c
    main/cron/cron_service.c
    main/gateway/ws_server.c
    main/heartbeat/heartbeat.c
    main/linux/linux_http.c
    main/linux/linux_log.c
    main/linux/linux_nvs.c
    main/linux/linux_paths.c
    main/llm/llm_proxy.c
    main/main.c
    main/memory/memory_store.c
    main/memory/session_mgr.c
    main/proxy/http_proxy.c
    main/skills/skill_loader.c
    main/telegram/telegram_bot.c
    main/tools/tool_cron.c
    main/tools/tool_files.c
    main/tools/tool_get_time.c
    main/tools/tool_registry.c
    main/tools/tool_run_javascript.c
    main/tools/tool_web_search.c
)

if(LIBWEBSOCKETS_INCLUDE_DIR AND LIBWEBSOCKETS_LIBRARY)
    list(APPEND MIMICLAW_SOURCES main/linux/linux_ws.c)
endif()

add_executable(mimiclaw ${MIMICLAW_SOURCES})

target_compile_definitions(mimiclaw PRIVATE _GNU_SOURCE)
if(LIBWEBSOCKETS_INCLUDE_DIR AND LIBWEBSOCKETS_LIBRARY)
    target_compile_definitions(mimiclaw PRIVATE MIMI_HAVE_LIBWEBSOCKETS)
endif()

target_compile_options(mimiclaw PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wno-unused-variable
)

target_include_directories(mimiclaw PRIVATE
    ${CMAKE_SOURCE_DIR}/main
    ${CURL_INCLUDE_DIRS}
    ${CJSON_INCLUDE_DIR}
)
if(LIBWEBSOCKETS_INCLUDE_DIR)
    target_include_directories(mimiclaw PRIVATE ${LIBWEBSOCKETS_INCLUDE_DIR})
endif()

target_link_libraries(mimiclaw PRIVATE
    Threads::Threads
    CURL::libcurl
    ${CJSON_LIBRARY}
)
if(LIBWEBSOCKETS_LIBRARY)
    target_link_libraries(mimiclaw PRIVATE ${LIBWEBSOCKETS_LIBRARY})
endif()

message(STATUS "")
message(STATUS "MimiClaw Linux Build Configuration:")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  cJSON include: ${CJSON_INCLUDE_DIR}")
message(STATUS "  cJSON library: ${CJSON_LIBRARY}")
if(LIBWEBSOCKETS_INCLUDE_DIR AND LIBWEBSOCKETS_LIBRARY)
    message(STATUS "  WebSocket backend: libwebsockets enabled")
else()
    message(STATUS "  WebSocket backend: disabled (libwebsockets not found)")
endif()
message(STATUS "")
