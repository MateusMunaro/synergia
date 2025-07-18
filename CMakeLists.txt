cmake_minimum_required(VERSION 3.14)
project(sinergia C)

set(CMAKE_C_STANDARD 11)

# Adicionar flags necessárias
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread -D_GNU_SOURCE")

# Habilita o uso do pkg-config
find_package(PkgConfig REQUIRED)

# Localiza as bibliotecas
pkg_check_modules(LIBWEBSOCKETS REQUIRED libwebsockets)
pkg_check_modules(JANSSON REQUIRED jansson)

# Inclui os diretórios das bibliotecas
include_directories(
        ${LIBWEBSOCKETS_INCLUDE_DIRS}
        ${JANSSON_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/include
)

# Diretórios para linkage
link_directories(
        ${LIBWEBSOCKETS_LIBRARY_DIRS}
        ${JANSSON_LIBRARY_DIRS}
)

# Define o executável
add_executable(sinergia
        src/main.c
        src/utils.c
        src/operation.c
        src/log.c
        src/versioning.c
        src/websocket_client.c
        src/plataform_utils.c
        src/file_watcher.c
        include/operation.h
        include/versioning.h
        include/log.h
        include/websocket_client.h
        include/utils.h
        include/plataform_utils.h
        include/file_watcher.h
)

# Faz o link das bibliotecas com o executável
target_link_libraries(sinergia
        ${LIBWEBSOCKETS_LIBRARIES}
        ${JANSSON_LIBRARIES}
        pthread
)