# snt_server — authoritative server module (engine-agnostic C++).
#
# Current multiplayer protocol: docs/游戏网络协议设计.md.
#
# Contains: protocol codec, command queue, network transport (TCP/UDP),
# ServerCore (session + command orchestration), NetworkClient (client-side).
# Does NOT depend on Godot — the GDExtension bindings live in src/bindings/.

add_library(snt_server STATIC)

target_include_directories(snt_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(snt_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(snt_server PUBLIC snt_core)

# Platform-specific link libraries for sockets.
if(WIN32)
    target_link_libraries(snt_server PUBLIC ws2_32)
endif()

if(MSVC)
    target_compile_options(snt_server PRIVATE /FS /utf-8)
endif()

target_sources(snt_server PRIVATE
    protocol/protocol_types.hpp
    protocol/protocol_codec.hpp
    protocol/protocol_codec.cpp
    command_queue.hpp
    command_queue.cpp
    transport/socket_platform.hpp
    transport/socket_platform.cpp
    transport/tcp_connection.hpp
    transport/tcp_connection.cpp
    transport/udp_socket.hpp
    transport/udp_socket.cpp
    server_core.hpp
    server_core.cpp
    network_client.hpp
    network_client.cpp
)
