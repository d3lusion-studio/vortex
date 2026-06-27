find_package(Threads REQUIRED)

vortex_add_module(jobs
    SOURCES
        src/job_system.cpp
    PUBLIC_DEPS
        Vortex::core
)

target_link_libraries(vortex_jobs PUBLIC Threads::Threads)
