#include <scene/compile/terrain_lod_selector.h>

#include <math/screen_space_metrics.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_map>

namespace wz::scene
{
    namespace
    {
        struct VisibleChunk
        {
            TerrainChunkInfo info{};
            wz::math::ProjectionResult projection{};
            float projected_area_px = 0.0f;
            std::vector<const TerrainLodRecord*> lods;
            std::vector<float> errors_px;
            uint32_t selected = 0;
        };

        struct RefinementCandidate
        {
            uint32_t chunk_index = 0;
            uint32_t current = 0;
            uint32_t target = 0;
            uint32_t cost = 0;
            float priority = 0.0f;

            bool operator<(const RefinementCandidate& other) const noexcept
            {
                return priority < other.priority;
            }
        };

        constexpr uint32_t kUnlimitedBudget =
            (std::numeric_limits<uint32_t>::max)();

        bool budget_is_unlimited(uint32_t budget) noexcept
        {
            return budget == kUnlimitedBudget;
        }

        uint32_t lod_level_value(
            const TerrainLodRecord* lod) noexcept
        {
            return lod ? lod->lod_id.value : 0u;
        }

        uint32_t lod_triangles(
            const TerrainLodRecord* lod) noexcept
        {
            return lod ? lod->triangle_count : 0u;
        }

        uint32_t coarsest_surfel_cost(const TerrainChunkInfo& chunk) noexcept
        {
            for (uint32_t i =
                     static_cast<uint32_t>(
                         chunk.surfel_density_levels.size());
                 i > 0u;
                 --i)
            {
                const TerrainSurfelDensityLevel& level =
                    chunk.surfel_density_levels[i - 1u];
                if (level.valid) {
                    return level.equivalent_triangle_cost;
                }
            }
            return 0u;
        }

        float lod_error(
            const TerrainLodRecord* lod,
            const wz::math::ProjectionResult& projection,
            const TerrainLodSelectionParams& params) noexcept
        {
            return wz::math::world_error_to_pixels(
                lod ? lod->conservative_geometric_error : 0.0f,
                projection.nearest_depth,
                params.viewport_height,
                params.fov_y_radians);
        }

        bool lods_are_finer_first(
            const TerrainLodRecord* a,
            const TerrainLodRecord* b) noexcept
        {
            return lod_level_value(a) < lod_level_value(b);
        }

        uint32_t coarsest_threshold_lod(
            const VisibleChunk& chunk,
            const TerrainLodSelectionParams& params) noexcept
        {
            for (uint32_t i = static_cast<uint32_t>(chunk.lods.size());
                 i > 0u;
                 --i)
            {
                const uint32_t index = i - 1u;
                if (chunk.errors_px[index] <= params.pixel_error_threshold) {
                    return index;
                }
            }
            return 0u;
        }

        RefinementCandidate make_candidate(
            const VisibleChunk& chunk,
            uint32_t chunk_index)
        {
            if (chunk.selected == 0u) {
                return {};
            }

            const uint32_t target = chunk.selected - 1u;
            const uint32_t current_triangles =
                lod_triangles(chunk.lods[chunk.selected]);
            const uint32_t target_triangles =
                lod_triangles(chunk.lods[target]);
            if (target_triangles <= current_triangles) {
                return {};
            }

            const float benefit =
                (std::max)(
                    0.0f,
                    chunk.errors_px[chunk.selected] - chunk.errors_px[target]);
            const uint32_t cost = target_triangles - current_triangles;
            const float priority =
                cost == 0u
                    ? 0.0f
                    : (benefit / static_cast<float>(cost))
                        * (std::max)(0.0f, chunk.projected_area_px);

            return RefinementCandidate{
                .chunk_index = chunk_index,
                .current = chunk.selected,
                .target = target,
                .cost = cost,
                .priority = priority,
            };
        }

        const TerrainLodChoice* find_previous_choice(
            std::span<const TerrainLodChoice> previous,
            uint32_t terrain_instance_index,
            TerrainChunkId chunk_id) noexcept
        {
            for (const TerrainLodChoice& choice : previous) {
                if (choice.terrain_instance_index == terrain_instance_index
                    && choice.chunk_id == chunk_id)
                {
                    return &choice;
                }
            }
            return nullptr;
        }

        uint32_t find_lod_index(
            const VisibleChunk& chunk,
            TerrainLodId lod_id) noexcept
        {
            for (uint32_t i = 0u; i < chunk.lods.size(); ++i) {
                if (chunk.lods[i]->lod_id.value == lod_id.value) {
                    return i;
                }
            }
            return (std::numeric_limits<uint32_t>::max)();
        }

        bool try_apply_hysteresis(
            VisibleChunk& chunk,
            const TerrainLodChoice& previous,
            const TerrainLodSelectionParams& params,
            uint32_t& total_triangles) noexcept
        {
            const uint32_t previous_index =
                find_lod_index(chunk, previous.lod_id);
            if (previous_index >= chunk.lods.size()
                || previous_index == chunk.selected)
            {
                return false;
            }

            const float margin =
                (std::max)(0.0f, params.hysteresis_fraction);
            const float lower =
                params.pixel_error_threshold * (1.0f - margin);
            const float upper =
                params.pixel_error_threshold * (1.0f + margin);

            bool keep_previous = false;
            if (chunk.selected > previous_index) {
                keep_previous = chunk.errors_px[chunk.selected] > lower;
            }
            else {
                keep_previous = chunk.errors_px[previous_index] < upper;
            }

            if (!keep_previous) {
                return false;
            }

            const uint32_t selected_triangles =
                lod_triangles(chunk.lods[chunk.selected]);
            const uint32_t previous_triangles =
                lod_triangles(chunk.lods[previous_index]);
            if (!budget_is_unlimited(params.triangle_budget)
                && previous_triangles > selected_triangles)
            {
                const uint32_t additional =
                    previous_triangles - selected_triangles;
                if (total_triangles + additional > params.triangle_budget) {
                    return false;
                }
                total_triangles += additional;
            }
            else {
                total_triangles -=
                    (std::min)(total_triangles, selected_triangles);
                total_triangles += previous_triangles;
            }

            chunk.selected = previous_index;
            return true;
        }

        bool neighbor_id_matches(TerrainChunkId a, TerrainChunkId b) noexcept
        {
            return a == b && a != kInvalidTerrainChunkId;
        }

        bool are_neighbors(
            const VisibleChunk& a,
            const VisibleChunk& b) noexcept
        {
            return neighbor_id_matches(a.info.boundary.negative_x_neighbor, b.info.chunk_id)
                || neighbor_id_matches(a.info.boundary.positive_x_neighbor, b.info.chunk_id)
                || neighbor_id_matches(a.info.boundary.negative_z_neighbor, b.info.chunk_id)
                || neighbor_id_matches(a.info.boundary.positive_z_neighbor, b.info.chunk_id)
                || neighbor_id_matches(b.info.boundary.negative_x_neighbor, a.info.chunk_id)
                || neighbor_id_matches(b.info.boundary.positive_x_neighbor, a.info.chunk_id)
                || neighbor_id_matches(b.info.boundary.negative_z_neighbor, a.info.chunk_id)
                || neighbor_id_matches(b.info.boundary.positive_z_neighbor, a.info.chunk_id);
        }

        void enforce_neighbor_constraints(
            std::vector<VisibleChunk>& chunks,
            const TerrainLodSelectionParams& params,
            uint32_t& total_triangles)
        {
            if (!params.enforce_neighbor_lod_delta) {
                return;
            }

            // Keep this pass intentionally simple until #127 settles seam policy
            // and #126 establishes real chunk counts. Replace with chunk-id
            // adjacency if large terrain proxies make this all-pairs walk hot.
            bool changed = true;
            while (changed) {
                changed = false;
                for (uint32_t i = 0u; i < chunks.size(); ++i) {
                    for (uint32_t j = i + 1u; j < chunks.size(); ++j) {
                        if (!are_neighbors(chunks[i], chunks[j])) {
                            continue;
                        }

                        VisibleChunk* coarser = &chunks[i];
                        VisibleChunk* finer = &chunks[j];
                        if (coarser->selected < finer->selected) {
                            std::swap(coarser, finer);
                        }

                        if (coarser->selected <= finer->selected
                            + params.max_neighbor_lod_delta)
                        {
                            continue;
                        }
                        if (coarser->selected == 0u) {
                            continue;
                        }

                        const uint32_t target = coarser->selected - 1u;
                        const uint32_t current_triangles =
                            lod_triangles(coarser->lods[coarser->selected]);
                        const uint32_t target_triangles =
                            lod_triangles(coarser->lods[target]);
                        const uint32_t additional =
                            target_triangles > current_triangles
                                ? target_triangles - current_triangles
                                : 0u;
                        if (!budget_is_unlimited(params.triangle_budget)
                            && total_triangles + additional
                                > params.triangle_budget)
                        {
                            continue;
                        }

                        coarser->selected = target;
                        total_triangles += additional;
                        changed = true;
                    }
                }
            }
        }

        const TerrainSurfelDensityLevel* select_surfel_density(
            const VisibleChunk& chunk,
            const TerrainLodSelectionParams& params) noexcept
        {
            if (!params.enable_surfel_lods
                || chunk.info.surfel_density_levels.empty()
                || chunk.lods.empty()
                || chunk.selected + 1u != chunk.lods.size()
                || chunk.errors_px[chunk.selected]
                    > params.pixel_error_threshold)
            {
                return nullptr;
            }

            const float radius_depth =
                chunk.projection.farthest_depth > 0.0f
                    ? chunk.projection.farthest_depth
                    : chunk.projection.nearest_depth;
            const float min_projected_radius_px =
                (std::max)(1.0f, params.surfel_target_coverage_px);
            const TerrainSurfelDensityLevel* fallback = nullptr;
            for (const TerrainSurfelDensityLevel& level :
                 chunk.info.surfel_density_levels)
            {
                if (!level.valid) {
                    continue;
                }
                fallback = &level;
                const float projected_radius_px =
                    wz::math::world_error_to_pixels(
                        level.representative_radius,
                        radius_depth,
                        params.viewport_height,
                        params.fov_y_radians);
                if (projected_radius_px >= min_projected_radius_px) {
                    return &level;
                }
            }
            return fallback;
        }

        float screen_triangle_density(
            const VisibleChunk& chunk,
            uint32_t triangle_count) noexcept
        {
            if (chunk.projected_area_px <= 0.0f) {
                return 0.0f;
            }
            return static_cast<float>(triangle_count)
                / chunk.projected_area_px;
        }

        bool exceeds_density_limits(
            const VisibleChunk& chunk,
            const TerrainLodSelectionParams& params,
            uint32_t selected_triangle_count) noexcept
        {
            if (params.max_asset_triangle_density > 0.0f
                && chunk.info.asset_triangle_density
                    > params.max_asset_triangle_density)
            {
                return true;
            }

            if (params.max_screen_triangle_density > 0.0f
                && screen_triangle_density(chunk, selected_triangle_count)
                    > params.max_screen_triangle_density)
            {
                return true;
            }

            return false;
        }

        TerrainLodChoice make_choice(
            const VisibleChunk& chunk,
            const TerrainLodSelectionParams& params)
        {
            const TerrainLodRecord* lod =
                chunk.lods[chunk.selected];
            if (const TerrainSurfelDensityLevel* surfel =
                    select_surfel_density(chunk, params))
            {
                return TerrainLodChoice{
                    .terrain_instance_index =
                        chunk.info.terrain_instance_index,
                    .chunk_id = chunk.info.chunk_id,
                    .representation_kind =
                        TerrainVisualRepresentationKind::SurfelCloud,
                    .lod_id = TerrainLodId{ surfel->density_id.value },
                    .projected_error_px = chunk.errors_px[chunk.selected],
                    .projected_area_px = chunk.projected_area_px,
                    .asset_triangle_density =
                        chunk.info.asset_triangle_density,
                    .screen_triangle_density =
                        screen_triangle_density(
                            chunk,
                            surfel->equivalent_triangle_cost),
                    .priority = wz::math::lod_priority(
                        chunk.errors_px[chunk.selected],
                        chunk.projected_area_px,
                        static_cast<float>(
                            surfel->equivalent_triangle_cost)),
                };
            }
            return TerrainLodChoice{
                .terrain_instance_index = chunk.info.terrain_instance_index,
                .chunk_id = chunk.info.chunk_id,
                .representation_kind = chunk.info.representation_kind,
                .lod_id = TerrainLodId{ lod->lod_id.value },
                .projected_error_px = chunk.errors_px[chunk.selected],
                .projected_area_px = chunk.projected_area_px,
                .asset_triangle_density =
                    chunk.info.asset_triangle_density,
                .screen_triangle_density =
                    screen_triangle_density(chunk, lod_triangles(lod)),
                .priority = wz::math::lod_priority(
                    chunk.errors_px[chunk.selected],
                    chunk.projected_area_px,
                static_cast<float>(lod->triangle_count)),
            };
        }

        float selected_lod_priority(const VisibleChunk& chunk) noexcept
        {
            return wz::math::lod_priority(
                chunk.errors_px[chunk.selected],
                chunk.projected_area_px,
                static_cast<float>(
                    lod_triangles(chunk.lods[chunk.selected])));
        }

        void evict_lowest_priority_chunks_to_budget(
            std::vector<VisibleChunk>& chunks,
            uint32_t& total_triangles,
            uint32_t triangle_budget)
        {
            while (total_triangles > triangle_budget && !chunks.empty()) {
                auto worst = chunks.begin();
                for (auto it = chunks.begin() + 1; it != chunks.end(); ++it) {
                    const float it_priority = selected_lod_priority(*it);
                    const float worst_priority =
                        selected_lod_priority(*worst);
                    if (it_priority < worst_priority) {
                        worst = it;
                        continue;
                    }
                    if (it_priority == worst_priority) {
                        const uint32_t it_cost =
                            lod_triangles(it->lods[it->selected]);
                        const uint32_t worst_cost =
                            lod_triangles(worst->lods[worst->selected]);
                        if (it_cost > worst_cost
                            || (it_cost == worst_cost
                                && it->projected_area_px
                                    < worst->projected_area_px))
                        {
                            worst = it;
                        }
                    }
                }

                total_triangles -=
                    (std::min)(
                        total_triangles,
                        lod_triangles(worst->lods[worst->selected]));
                chunks.erase(worst);
            }
        }
    }

    std::vector<TerrainLodChoice> select_terrain_lods(
        std::span<const TerrainChunkInfo> chunks,
        const TerrainLodSelectionParams& params,
        const ViewData& view,
        std::span<const TerrainLodChoice> previous)
    {
        std::vector<TerrainLodChoice> out;
        if (chunks.empty()
            || params.triangle_budget == 0u
            || params.viewport_width <= 0.0f
            || params.viewport_height <= 0.0f)
        {
            return out;
        }

        std::vector<VisibleChunk> visible;
        visible.reserve(chunks.size());

        uint32_t total_triangles = 0u;
        for (const TerrainChunkInfo& chunk : chunks) {
            if (chunk.lods.empty()) {
                continue;
            }

            const wz::math::ProjectionResult projection =
                wz::math::project_aabb_to_screen_rect(
                    chunk.world_bounds,
                    view.view_projection,
                    params.viewport_width,
                    params.viewport_height);
            const float area =
                wz::math::chunk_projected_area_pixels(projection);
            if (area <= 0.0f) {
                continue;
            }

            VisibleChunk visible_chunk{
                .info = chunk,
                .projection = projection,
                .projected_area_px = area,
            };
            visible_chunk.lods.reserve(chunk.lods.size());
            for (const TerrainLodRecord& lod : chunk.lods) {
                if (lod.valid) {
                    visible_chunk.lods.push_back(&lod);
                }
            }
            if (visible_chunk.lods.empty()) {
                continue;
            }
            std::sort(
                visible_chunk.lods.begin(),
                visible_chunk.lods.end(),
                lods_are_finer_first);

            visible_chunk.errors_px.reserve(visible_chunk.lods.size());
            for (const TerrainLodRecord* lod : visible_chunk.lods) {
                visible_chunk.errors_px.push_back(
                    lod_error(lod, projection, params));
            }

            visible_chunk.selected =
                static_cast<uint32_t>(visible_chunk.lods.size() - 1u);
            const uint32_t base_mesh_cost =
                lod_triangles(visible_chunk.lods[visible_chunk.selected]);
            const uint32_t base_surfel_cost =
                params.enable_surfel_lods
                    ? coarsest_surfel_cost(visible_chunk.info)
                    : 0u;
            const uint32_t base_cost =
                base_surfel_cost > 0u
                    ? (std::min)(base_mesh_cost, base_surfel_cost)
                    : base_mesh_cost;
            total_triangles += base_cost;
            visible.push_back(std::move(visible_chunk));
        }

        if (!budget_is_unlimited(params.triangle_budget)
            && total_triangles > params.triangle_budget)
        {
            evict_lowest_priority_chunks_to_budget(
                visible,
                total_triangles,
                params.triangle_budget);
        }

        std::priority_queue<RefinementCandidate> candidates;
        for (uint32_t i = 0u; i < visible.size(); ++i) {
            const RefinementCandidate candidate = make_candidate(visible[i], i);
            if (candidate.cost > 0u && candidate.priority > 0.0f) {
                candidates.push(candidate);
            }
        }

        while (!candidates.empty()) {
            const RefinementCandidate candidate = candidates.top();
            candidates.pop();

            VisibleChunk& chunk = visible[candidate.chunk_index];
            if (chunk.selected != candidate.current) {
                continue;
            }
            if (chunk.selected <= coarsest_threshold_lod(chunk, params)) {
                continue;
            }
            if (!budget_is_unlimited(params.triangle_budget)
                && total_triangles + candidate.cost > params.triangle_budget)
            {
                continue;
            }

            chunk.selected = candidate.target;
            total_triangles += candidate.cost;

            const RefinementCandidate next =
                make_candidate(chunk, candidate.chunk_index);
            if (next.cost > 0u && next.priority > 0.0f) {
                candidates.push(next);
            }
        }

        for (VisibleChunk& chunk : visible) {
            if (const TerrainLodChoice* previous_choice =
                    find_previous_choice(
                        previous,
                        chunk.info.terrain_instance_index,
                        chunk.info.chunk_id))
            {
                try_apply_hysteresis(
                    chunk,
                    *previous_choice,
                    params,
                    total_triangles);
            }
        }

        enforce_neighbor_constraints(visible, params, total_triangles);

        out.reserve(visible.size());
        for (const VisibleChunk& chunk : visible) {
            const uint32_t selected_triangles =
                lod_triangles(chunk.lods[chunk.selected]);
            if (exceeds_density_limits(
                    chunk,
                    params,
                    selected_triangles))
            {
                continue;
            }
            out.push_back(make_choice(chunk, params));
        }
        return out;
    }
}
