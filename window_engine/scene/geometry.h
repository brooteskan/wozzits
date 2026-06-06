#pragma once

// wz/scene/geometry.h

#include <math/math_types.h>
#include <math/mat4.h>

#include <algorithm>
#include <cfloat>

namespace wz::scene {

    struct AABB {
        wz::math::Vec3 min{};
        wz::math::Vec3 max{};
    };

    namespace detail {

        inline AABB include_point(AABB out, const wz::math::Vec3& p)
        {
            out.min.x = (std::min)(out.min.x, p.x);
            out.min.y = (std::min)(out.min.y, p.y);
            out.min.z = (std::min)(out.min.z, p.z);
            out.max.x = (std::max)(out.max.x, p.x);
            out.max.y = (std::max)(out.max.y, p.y);
            out.max.z = (std::max)(out.max.z, p.z);
            return out;
        }

    } // namespace detail

    inline AABB transform_aabb(const AABB& local, const wz::math::Mat4& world)
    {
        const wz::math::Vec3 corners[8] = {
            { local.min.x, local.min.y, local.min.z },
            { local.max.x, local.min.y, local.min.z },
            { local.min.x, local.max.y, local.min.z },
            { local.max.x, local.max.y, local.min.z },
            { local.min.x, local.min.y, local.max.z },
            { local.max.x, local.min.y, local.max.z },
            { local.min.x, local.max.y, local.max.z },
            { local.max.x, local.max.y, local.max.z },
        };

        AABB out{
            .min = { FLT_MAX, FLT_MAX, FLT_MAX },
            .max = { -FLT_MAX, -FLT_MAX, -FLT_MAX },
        };

        for (const wz::math::Vec3& corner : corners)
            out = detail::include_point(out, wz::math::mul_point(world, corner));

        return out;
    }

} // namespace wz::scene
