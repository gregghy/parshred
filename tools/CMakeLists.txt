# ── CLI tool ───────────────────────────────────────────────────────────
add_executable(parshred_cli parshred_cli.cpp)
target_link_libraries(parshred_cli PRIVATE parshred)
set_target_properties(parshred_cli PROPERTIES OUTPUT_NAME "parshred")
