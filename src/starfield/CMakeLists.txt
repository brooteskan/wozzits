# wz::engine::starfield -- star-catalog import/convert utilities, their own
# subsystem tree OUTSIDE engine/ (headers at window_engine/starfield/): pure-CPU
# astronomy + the PLY catalog parser + the .star_catalog.json sidecar codec.
# Consumed by the star_catalog asset compiler (engine/assets/star_catalog);
# runtime never calls them. Compiled into window_engine, same pattern as
# src/cognition. (The Tycho-2 parser + wozzits_tycho2_prep CLI were extracted
# out of the repo -- see D:/code/wozzits/tycho2/extracted-from-wozzits/.)
target_sources(window_engine PRIVATE
    star_catalog.cpp
    star_catalog_serialize.cpp
    star_catalog_ply_importer.cpp
)
