cmake_minimum_required(VERSION 3.28)
project(downloadProject)

set(CMAKE_CXX_STANDARD 17)
include_directories("D:/boost_1_87_0")
link_directories("D:/boost_1_87_0/stage/lib")
include_directories("D:/OpenSSL-Win64/include")
include_directories("E:/spdlog-1.x/include")
link_directories("D:/OpenSSL-Win64/lib/VC/x64/MT")
add_compile_options(-Wa,-mbig-obj)

add_executable(downloadProject main.cpp
        tcp_client.cpp
        tcp_client.h
        downloader.cpp
        downloader.h
        DownloadTask.h)

target_link_libraries(downloadProject boost_system-mgw13-mt-x64-1_87.dll ws2_32 libssl.lib libcrypto.lib)