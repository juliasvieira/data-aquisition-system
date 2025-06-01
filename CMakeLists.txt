cmake_minimum_required(VERSION 3.10)
project(data-acquisition-system)

# Configurações do C++
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configurações para threads
set(CMAKE_THREAD_PREFER_PTHREAD ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Encontra a biblioteca Boost
find_package(Boost 1.65.1 REQUIRED COMPONENTS system)

# Configurações de debug
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra")

# Diretórios de include
include_directories(${Boost_INCLUDE_DIRS} src)

# Executável principal
add_executable(das src/main.cpp)

# Link das bibliotecas
target_link_libraries(das ${Boost_LIBRARIES})
target_link_libraries(das Threads::Threads)

# Informações de build
message(STATUS "Boost version: ${Boost_VERSION}")
message(STATUS "Boost libraries: ${Boost_LIBRARIES}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
