cmake_minimum_required(VERSION 3.0.0)

PROJECT(node_0)

INCLUDE(../../cmake-scripts/common.cmake)
INCLUDE(${CMAKELISTS_EXPORT_DIRECTORY}/boost.cmake)
INCLUDE(${CMAKELISTS_EXPORT_DIRECTORY}/base.cmake OPTIONAL) #el optional es por si el target ya esta presente y no hemos generado
INCLUDE(${CMAKELISTS_EXPORT_DIRECTORY}/models.cmake OPTIONAL) #el optional es por si el target ya esta presente y no hemos generado
INCLUDE(${CMAKE_SCRIPTS_DIR}/flatc.cmake)
include(${CMAKELISTS_EXPORT_DIRECTORY}/ZeroMQConfig.cmake)

find_package(Boost )#COMPONENTS regex)
include_directories(${Boost_INCLUDE_DIRS})

include_directories(${CIVSIM_DIRECTORY}/base/include)
include_directories(${CIVSIM_DIRECTORY}/models/include)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

file(GLOB_RECURSE HEADER_FILES src/*.h)
file(GLOB_RECURSE SOURCE_FILES src/*.cpp)
set(REMOTESIMULATION_IDL_FILES
	server.fbs
)
call_flatbuffers_compiler(${REMOTESIMULATION_IDL_FILES})
add_custom_target(FLATBUFFERS_COMPILER DEPENDS ${REMOTESIMULATION_IDL_FILES}  ${GENERATED_FLATBUFFERS_FILES} )


add_executable(node_0
	${HEADER_FILES} 
	${SOURCE_FILES}
	)
	
target_link_libraries(node_0 libzmq)
