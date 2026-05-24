// src/engine/assets/scalar_field/scalar_field_set.cpp

#include <engine/assets/scalar_field/scalar_field_set.h>

#include <utility>

namespace wz::engine::assets
{
    bool ScalarFieldSet::add(const std::string& name, ScalarFieldData field)
    {
        if (name.empty())
            return false;

        if (!field.valid())
            return false;

        if (fields_.contains(name))
            return false;

        // First insertion establishes dimensions.
        if (fields_.empty())
        {
            width_  = field.width;
            height_ = field.height;
            depth_  = field.depth;
        }
        else
        {
            // All subsequent fields must match.
            if (field.width  != width_  ||
                field.height != height_ ||
                field.depth  != depth_)
                return false;
        }

        fields_.emplace(name, std::move(field));
        return true;
    }

    const ScalarFieldData* ScalarFieldSet::get(const std::string& name) const
    {
        auto it = fields_.find(name);
        if (it == fields_.end())
            return nullptr;
        return &it->second;
    }

    bool ScalarFieldSet::contains(const std::string& name) const
    {
        return fields_.contains(name);
    }

    void ScalarFieldSet::clear()
    {
        fields_.clear();
        width_  = 0;
        height_ = 0;
        depth_  = 0;
    }

}  // namespace wz::engine::assets
