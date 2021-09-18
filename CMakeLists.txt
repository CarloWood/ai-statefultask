cmake_minimum_required(VERSION 3.12...3.16)

project(statefultask
   DESCRIPTION "Asynchronous, Stateful Task Scheduler library"
   LANGUAGES CXX
)

include(AICxxProject)

add_subdirectory(doc)

#==============================================================================
# BUILD PROJECT
#

# This project is an OBJECT-library, used by other git submodules and the main project.
add_library(statefultask_ObjLib OBJECT)

# Set SOURCE_DIR to the current directory.
set_property(
  TARGET statefultask_ObjLib
  PROPERTY SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
)

# The list of source files.
target_sources(statefultask_ObjLib
  PRIVATE
    "AIEngine.cxx"
    "AIStatefulTask.cxx"
    "AIStatefulTaskMutex.cxx"
    "AITimer.cxx"
    "Broker.cxx"
    "DefaultMemoryPagePool.cxx"

    "AIDelayedFunction.h"
    "AIEngine.h"
    "AIFriendOfStatefulTask.h"
    "AIPackagedTask.h"
    "AIStatefulTask.h"
    "AIStatefulTaskMutex.h"
    "AITimer.h"
    "Broker.h"
    "BrokerKey.h"
    "DefaultMemoryPagePool.h"
)

# Required include search-paths.
get_target_property(CWDS_INTERFACE_INCLUDE_DIRECTORIES AICxx::cwds INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(statefultask_ObjLib
  PUBLIC
    "${CWDS_INTERFACE_INCLUDE_DIRECTORIES}" # For sys.h and debug.h.
)

# Require support for C++20.
target_compile_features(statefultask_ObjLib
  PUBLIC cxx_std_20
)

# Set link dependencies.
target_link_libraries(statefultask_ObjLib
  PUBLIC
    AICxx::evio
)

# Create an ALIAS target.
add_library(AICxx::statefultask ALIAS statefultask_ObjLib)

# Prepend this object library to the list.
set(AICXX_OBJECTS_LIST AICxx::statefultask ${AICXX_OBJECTS_LIST} CACHE INTERNAL "List of OBJECT libaries that this project uses.")
