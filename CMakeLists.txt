cmake_minimum_required(VERSION 3.0)
project(API_CXX)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BUILD_DIR})
set(API_SRC ${CMAKE_CURRENT_SOURCE_DIR})

#openssl 1.0.2
set(ssl_102  ${CMAKE_CURRENT_SOURCE_DIR}/ssl_1.0.2)


#===============================================================
if (WIN32)
    set(platform_macro "-DWINDOWS")
elseif (UNIX)
    set(platform_macro "-DLINUX -D_GLIBCXX_USE_CXX11_ABI=0")
endif ()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${platform_macro} -fPIC  -std=c++11")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} -g -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} -O3")

#===============================================================
if (WIN32)
    link_directories(${API_SRC}/bin/mingw_x64)
elseif(UNIX)
    link_directories(${API_SRC}/bin/linux_x64/ABI0)
endif ()
include_directories(${API_SRC}/include)
include_directories(${ssl_102}/include)

 

if (EXISTS "/usr/lib/libssl.so.1.0.0") # archlinux 
    set(LIBSSL /usr/lib/libssl.so.1.0.0)
elseif (EXISTS ${ssl_102}/lib/libssl.so)
    set(LIBSSL ${ssl_102}/lib/libssl.so)
else ( )
    set(LIBSSL ssl)
endif ()

if (WIN32)
    set(platform_lib "ws2_32")
elseif (UNIX)
    set(platform_lib ${LIBSSL})
endif ()


#add_executable( mytest ${API_SRC}/mytest/*.cpp)
#target_link_libraries(mytest 
#    DolphinDBAPI 
#    ${platform_lib} 
#    pthread 
#    uuid)

#===============================================================
macro(TEST module)
    add_executable(${module} ${API_SRC}/test/${module}.cpp)
    target_link_libraries(${module} 
        DolphinDBAPI 
        ${platform_lib} 
        pthread 
        uuid)
endmacro()


TEST(StreamingPollingClientTester)
TEST(StreamingThreadedClientTester)
TEST(StreamingThreadPooledClientTester)
TEST(DolphinDBTest)
#TEST(StreamingThreadedClientTester)


message(STATUS "\n\
     info :     ${CMAKE_SOURCE_DIR}\n
     sources:    ${sources} \n
     platform_lib: ${platform_lib}\n
")

