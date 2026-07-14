# wz::engine::starfield -- star-catalog import/convert utilities, their own
# subsystem tree OUTSIDE engine/ (headers at window_engine/starfield/): pure-CPU
# astronomy + the catalog parsers (Tycho-2, PLY) + the .star_catalog.json sidecar
# codec. Consumed by the star_catalog asset compiler (engine/assets/star_catalog)
# and the wozzits_tycho2_prep CLI; runtime never calls them. Compiled into
# window_engine, same pattern as src/cognition.
target_sources(window_engine PRIVATE
    star_catalog.cpp
    star_catalog_serialize.cpp
    star_catalog_ply_importer.cpp
    tycho2.cpp
)
