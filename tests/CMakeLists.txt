add_executable(engine_sim_offline_cxx20_smoke cxx20_smoke.cpp)

target_link_libraries(
    engine_sim_offline_cxx20_smoke
    PRIVATE engine_sim_offline::build_options
)

add_test(
    NAME foundation.cxx20
    COMMAND engine_sim_offline_cxx20_smoke
)
