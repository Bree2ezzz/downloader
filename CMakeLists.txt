cmake_minimum_required(VERSION 3.28)
project(downloadProject)

set(CMAKE_CXX_STANDARD 17)
include_directories("D:/boost_1_87_0")
link_directories("D:/boost_1_87_0/stage/lib")



add_executable(downloadProject main.cpp
        tcp_client.cpp
        tcp_client.h
        downloader.cpp
        downloader.h
        DownloadTask.h)

target_link_libraries(downloadProject boost_system-mgw13-mt-x64-1_87.dll ws2_32)