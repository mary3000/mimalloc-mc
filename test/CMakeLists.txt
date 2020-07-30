cmake_minimum_required(VERSION 3.5)

macro(task name)
    string(REPLACE "/" "-" fmtname ${name})
    #message(STATUS ${name} ${fmtname})
    add_executable(${fmtname} ${name}/main.c)
    target_include_directories(${fmtname} PRIVATE ${CMAKE_SOURCE_DIR})
endmacro()

task(race_next/one_thread)
task(race_next/n_threads)
task(alloc_free)
