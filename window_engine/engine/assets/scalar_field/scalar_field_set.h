#pragma once

// engine/assets/scalar_field/scalar_field_set.h
//
// A named collection of co-registered ScalarFieldData objects.
//
// "Co-registered" means all fields share the same (width, height, depth)
// dimensions — they describe different attributes of the same spatial
// domain (e.g. multiple Gaea exports over the same terrain tile).
//
// ScalarFieldSet validates dimension agreement at insertion time.
// The set is intentionally simple: unordered string-keyed map,
// no ownership transfer, no GPU knowledge.

#include <engine/assets/scalar_field/scalar_field.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace wz::engine::assets
{
    class ScalarFieldSet
    {
    public:
        // Add a named field.  The first field inserted establishes the
        // required dimensions for all subsequent fields.
        //
        // Returns false (and does not insert) when:
        //   - name is empty
        //   - field is not valid()
        //   - field dimensions don't match existing entries
        //   - name already exists
        bool add(const std::string& name, ScalarFieldData field);

        // Look up a field by name.  Returns nullptr if not found.
        const ScalarFieldData* get(const std::string& name) const;

        // Number of named fields in the set.
        size_t size() const noexcept { return fields_.size(); }

        // True when no fields have been inserted.
        bool empty() const noexcept { return fields_.empty(); }

        // Dimensions established by the first insertion, or (0,0,0) when empty.
        uint32_t width()  const noexcept { return width_; }
        uint32_t height() const noexcept { return height_; }
        uint32_t depth()  const noexcept { return depth_; }

        // Check whether a name exists in the set.
        bool contains(const std::string& name) const;

        // Remove all entries and reset dimensions.
        void clear();

        // Iterate all (name, field) pairs.
        using const_iterator =
            std::unordered_map<std::string, ScalarFieldData>::const_iterator;
        const_iterator begin() const noexcept { return fields_.begin(); }
        const_iterator end()   const noexcept { return fields_.end(); }

    private:
        std::unordered_map<std::string, ScalarFieldData> fields_;
        uint32_t width_  = 0;
        uint32_t height_ = 0;
        uint32_t depth_  = 0;
    };

}  // namespace wz::engine::assets
