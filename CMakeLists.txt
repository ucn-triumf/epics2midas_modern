cmake_minimum_required(VERSION 3.5)
project(epics_fe)

# Check for MIDASSYS and EPICS environment variables
if (NOT DEFINED ENV{MIDASSYS})
   message(SEND_ERROR "MIDASSYS environment variable not defined.")
endif()
if (NOT DEFINED ENV{EPICSSYS})
   message(SEND_ERROR "EPICSSYS environment variable not defined.")
endif()
if (NOT DEFINED ENV{EPICS_HOST_ARCH})
   message(SEND_ERROR "EPICS_HOST_ARCH environment variable not defined.")
endif()

if (NOT TARGET midas::midas)
   find_package(Midas REQUIRED PATHS $ENV{MIDASSYS})
endif()

set(EPICSSYS $ENV{EPICSSYS})
set(EPICS_HOST_ARCH $ENV{EPICS_HOST_ARCH})

link_directories(${EPICSSYS}/lib/${EPICS_HOST_ARCH})
if (${CMAKE_SYSTEM_NAME} MATCHES Linux)
   set(LIBS ${LIBS} -lpthread -lutil -lrt -lca -ldl)
endif()
if (${CMAKE_SYSTEM_NAME} MATCHES Darwin)
   set(LIBS ${LIBS} -lutil -lca)
endif()

add_executable(epics_fe epics_fe.cxx)

target_include_directories(epics_fe PRIVATE
   ${EPICSSYS}/include
   ${EPICSSYS}/include/os/Linux
   ${EPICSSYS}/include/os/Darwin
   ${EPICSSYS}/include/compiler/gcc
   ${EPICSSYS}/include/compiler/clang
)

target_link_libraries(epics_fe midas::mfed ${LIBS})
