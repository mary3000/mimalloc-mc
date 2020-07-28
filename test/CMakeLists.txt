cmake_minimum_required(VERSION 3.5)

#project(main)

#get_filename_component(LIBPATH ".." ABSOLUTE)
#file(GLOB_RECURSE LIB_SOURCES ${LIB_PATH}/*)

macro(task name)
    string(REPLACE "/" "-" fmtname ${name})
    #message(STATUS ${name} ${fmtname})
    add_executable(${fmtname} ${name}/main.c)
    target_include_directories(${fmtname} PRIVATE ${CMAKE_SOURCE_DIR})
endmacro()

#task(main)
#task(unit)
#task(threads)
task(race_next/one_thread)
task(race_next/n_threads)
