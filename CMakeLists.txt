cmake_minimum_required(VERSION 3.10)
project(korelin C)

set(CMAKE_C_STANDARD 17)

set(BIN_DIR ${CMAKE_SOURCE_DIR}/bin)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BIN_DIR})

include_directories(src korelion)

add_executable(korelin 
        src/korelin.c
        src/kgc.c
        src/kgc.h
        src/klex.c
        src/klex.h
        src/kparser.c
        src/kparser.h
        src/kvm.c
        src/kvm.h
        src/kcode.c
        src/kcode.h
        src/kcache.c
        src/kcache.h
        src/kconst.h
        src/keditor.c
        src/keditor.h
        src/comeonjit.c
        src/comeonjit.h
        src/kstd.c
        src/kstd.h
        src/kapi.c
        src/kapi.h
)

if(WIN32)
    target_link_libraries(korelin ws2_32 wininet)
else()
    target_link_libraries(korelin dl pthread)
endif()