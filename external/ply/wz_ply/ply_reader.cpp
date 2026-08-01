// external/ply/src/ply_reader.cpp

#include <ply/ply_reader.h>

#include "../tinyply/tinyply.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <istream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace wz::external::ply
{
    namespace
    {
        ScalarType to_scalar_type(tinyply::Type type)
        {
            switch (type)
            {
            case tinyply::Type::INT8:    return ScalarType::Int8;
            case tinyply::Type::UINT8:   return ScalarType::UInt8;
            case tinyply::Type::INT16:   return ScalarType::Int16;
            case tinyply::Type::UINT16:  return ScalarType::UInt16;
            case tinyply::Type::INT32:   return ScalarType::Int32;
            case tinyply::Type::UINT32:  return ScalarType::UInt32;
            case tinyply::Type::FLOAT32: return ScalarType::Float32;
            case tinyply::Type::FLOAT64: return ScalarType::Float64;
            default:                     return ScalarType::Unknown;
            }
        }

        // On-disk size of one scalar of `type`, in a BINARY payload. Computed
        // here rather than read out of tinyply's PropertyTable so the guard
        // below does not depend on vendored internals.
        std::uint64_t binary_stride_of(tinyply::Type type)
        {
            switch (type)
            {
            case tinyply::Type::INT8:
            case tinyply::Type::UINT8:   return 1u;
            case tinyply::Type::INT16:
            case tinyply::Type::UINT16:  return 2u;
            case tinyply::Type::INT32:
            case tinyply::Type::UINT32:
            case tinyply::Type::FLOAT32: return 4u;
            case tinyply::Type::FLOAT64: return 8u;
            default:                     return 0u;
            }
        }

        double read_scalar_as_double(const uint8_t* data, tinyply::Type type)
        {
            switch (type)
            {
            case tinyply::Type::INT8:
                return static_cast<double>(*reinterpret_cast<const int8_t*>(data));

            case tinyply::Type::UINT8:
                return static_cast<double>(*reinterpret_cast<const uint8_t*>(data));

            case tinyply::Type::INT16:
                return static_cast<double>(*reinterpret_cast<const int16_t*>(data));

            case tinyply::Type::UINT16:
                return static_cast<double>(*reinterpret_cast<const uint16_t*>(data));

            case tinyply::Type::INT32:
                return static_cast<double>(*reinterpret_cast<const int32_t*>(data));

            case tinyply::Type::UINT32:
                return static_cast<double>(*reinterpret_cast<const uint32_t*>(data));

            case tinyply::Type::FLOAT32:
                return static_cast<double>(*reinterpret_cast<const float*>(data));

            case tinyply::Type::FLOAT64:
                return *reinterpret_cast<const double*>(data);

            default:
                throw std::runtime_error("Unsupported PLY scalar type");
            }
        }

        size_t scalar_size(tinyply::Type type)
        {
            switch (type)
            {
            case tinyply::Type::INT8:    return sizeof(int8_t);
            case tinyply::Type::UINT8:   return sizeof(uint8_t);
            case tinyply::Type::INT16:   return sizeof(int16_t);
            case tinyply::Type::UINT16:  return sizeof(uint16_t);
            case tinyply::Type::INT32:   return sizeof(int32_t);
            case tinyply::Type::UINT32:  return sizeof(uint32_t);
            case tinyply::Type::FLOAT32: return sizeof(float);
            case tinyply::Type::FLOAT64: return sizeof(double);
            default:                     return 0;
            }
        }

        bool is_supported_scalar_type(tinyply::Type type)
        {
            return scalar_size(type) != 0;
        }

        std::string make_property_key(const std::string& element_name, const std::string& property_name)
        {
            return element_name + "." + property_name;
        }

        struct RequestedProperty
        {
            std::string element_name;
            std::string property_name;
            tinyply::Type tinyply_type = tinyply::Type::INVALID;
            std::shared_ptr<tinyply::PlyData> data;
        };
    }

    ReadResult read_ply_stream(std::istream& stream)
    {
        ReadResult result;

        try
        {
            tinyply::PlyFile file;

            // parse_header's return value used to be DISCARDED (issue #310,
            // A4-C4). An unrecognised property type does not throw -- it becomes
            // Type::INVALID with a stride of ZERO, so tinyply's skip does
            // is.ignore(0), the field's real bytes are never consumed, and every
            // subsequent field in every subsequent row is read SHIFTED. Measured
            // before this change: a 2-row file whose true values were
            // x=10/11 y=20/21 z=30/31 imported as ok=1 with row 1 reading
            // (50, 11, 21) -- a clean 4-byte desync, no error, no log line.
            // This fires on merely UNUSUAL files, not just hostile ones: `half`
            // is a type real exporters emit.
            if (!file.parse_header(stream))
            {
                result.ok = false;
                result.error.message =
                    "PLY header is malformed or declares an unsupported "
                    "property type";
                return result;
            }

            Document document;

            std::vector<RequestedProperty> requested_properties;

            const std::vector<tinyply::PlyElement> elements = file.get_elements();

            // Refuse any header in which two (element, property) pairs share a
            // buffer key (issue #310, A4-C3).
            //
            // tinyply keys its destination buffers on
            // hash_fnv1a(element.name + property.name) with NO SEPARATOR
            // (tinyply.h:553, insert side tinyply.h:1113), so "vertex" + "x"
            // collides EXACTLY with "verte" + "xx" -- no hash brute-force, just
            // string concatenation. tinyply's own duplicate guard
            // (tinyply.h:1116) only fires when both colliding properties are
            // REQUESTED, and this wrapper skips list properties at request time,
            // so making the second one a list bypasses it entirely: the lookup
            // then finds the first property's 4-byte buffer and writes a
            // 255-element list into it. Measured before this change:
            // STATUS_HEAP_CORRUPTION (0xC0000374) from a 1151-byte file.
            //
            // Detecting the collision here keeps the fix in our wrapper and
            // leaves vendored tinyply untouched. Reconstructing the exact key
            // (plain concatenation) is deliberate -- a separator here would test
            // a different question than the one tinyply actually asks.
            {
                std::unordered_map<std::string, std::string> key_owner;
                for (const tinyply::PlyElement& element : elements)
                {
                    for (const tinyply::PlyProperty& p : element.properties)
                    {
                        const std::string key = element.name + p.name;
                        const std::string owner = element.name + "." + p.name;
                        const auto [it, inserted] =
                            key_owner.emplace(key, owner);
                        if (!inserted)
                        {
                            result.ok = false;
                            result.error.message =
                                "PLY header is ambiguous: '" + owner
                                + "' and '" + it->second
                                + "' share one internal buffer key";
                            return result;
                        }
                    }
                }
            }

            document.header.elements.reserve(elements.size());

            for (const tinyply::PlyElement& tinyply_element : elements)
            {
                Element element;
                element.name = tinyply_element.name;
                element.count = static_cast<uint64_t>(tinyply_element.size);

                for (const tinyply::PlyProperty& tinyply_property : tinyply_element.properties)
                {
                    Property property;
                    property.name = tinyply_property.name;
                    property.type = to_scalar_type(tinyply_property.propertyType);
                    property.is_list = tinyply_property.isList;

                    element.properties.push_back(property);

                    if (tinyply_property.isList)
                    {
                        // v1 wrapper policy:
                        // Keep list properties in the header, but do not read them into ScalarTable.
                        continue;
                    }

                    if (!is_supported_scalar_type(tinyply_property.propertyType))
                    {
                        continue;
                    }

                    RequestedProperty requested;
                    requested.element_name = tinyply_element.name;
                    requested.property_name = tinyply_property.name;
                    requested.tinyply_type = tinyply_property.propertyType;

                    requested.data = file.request_properties_from_element(
                        tinyply_element.name,
                        { tinyply_property.name });

                    requested_properties.push_back(std::move(requested));
                }

                document.header.elements.push_back(std::move(element));
            }

            // Bound every declared element count against the bytes that are
            // actually present, BEFORE handing the stream to tinyply (issue
            // #310, A4-C2/A4-C8).
            //
            // tinyply validates only that a count is not NEGATIVE
            // (tinyply.h:520). It then computes `count * stride` as an
            // unchecked size_t in two places (tinyply.h:856 per-property,
            // tinyply.h:1205 for the bulk read). With `element vertex
            // 4611686018427387905` (2^62+1) and three floats, count*12 wraps to
            // 12 -- so a 12-byte buffer is allocated, the bulk read asks for 12
            // bytes, the file supplies exactly 12, tinyply's EOF guard never
            // fires, and the scatter loop then runs 2^62 times writing past both
            // allocations. Measured before this change: ACCESS_VIOLATION from a
            // 145-byte file, and the 2^62 variant faults with ZERO payload
            // bytes.
            //
            // The guard lives HERE, in the wrapper, rather than in vendored
            // tinyply, and it covers BOTH entry points into this library --
            // the gaussian-splat importer and the star-catalog importer both
            // come through read_ply_bytes/read_ply_stream.
            //
            // Expressed as a DIVISION so the bound cannot itself overflow.
            {
                const std::streampos data_begin = stream.tellg();
                stream.seekg(0, std::ios::end);
                const std::streampos stream_end = stream.tellg();
                stream.seekg(data_begin);

                const std::uint64_t remaining_bytes =
                    (stream_end > data_begin)
                        ? static_cast<std::uint64_t>(stream_end - data_begin)
                        : 0u;

                const bool binary = file.is_binary_file();

                for (const tinyply::PlyElement& element : elements)
                {
                    const std::uint64_t count =
                        static_cast<std::uint64_t>(element.size);
                    if (count == 0u)
                        continue;

                    // Minimum bytes one row of this element can occupy. For a
                    // BINARY payload that is the exact fixed stride (list
                    // properties add at least their count field on top, so
                    // using the fixed part alone stays a valid lower bound).
                    //
                    // For ASCII it is NOT the binary stride -- "0 0 0\n" is six
                    // bytes for three doubles whose binary stride is 24 -- so
                    // using the stride there would reject legitimate files.
                    // One byte per row is the only safe universal bound, and it
                    // is still sufficient: the overflow attack needs a count
                    // astronomically larger than any file.
                    std::uint64_t min_row_bytes = 1u;
                    if (binary) {
                        std::uint64_t stride = 0u;
                        for (const tinyply::PlyProperty& p : element.properties)
                            stride += p.isList
                                          ? binary_stride_of(p.listType)
                                          : binary_stride_of(p.propertyType);
                        if (stride > min_row_bytes)
                            min_row_bytes = stride;
                    }

                    if (count > remaining_bytes / min_row_bytes)
                    {
                        result.ok = false;
                        result.error.message =
                            "PLY element '" + element.name
                            + "' declares " + std::to_string(count)
                            + " entries, which cannot fit in the "
                            + std::to_string(remaining_bytes)
                            + " bytes of payload present";
                        return result;
                    }
                }
            }

            file.read(stream);

            for (const Element& element : document.header.elements)
            {
                ScalarTable table;
                table.element_name = element.name;
                table.row_count = element.count;

                for (const Property& property : element.properties)
                {
                    if (!property.is_list && property.type != ScalarType::Unknown)
                    {
                        table.properties.push_back(property);
                    }
                }

                if (table.properties.empty() || table.row_count == 0)
                {
                    continue;
                }

                table.values.resize(
                    static_cast<size_t>(table.row_count) * table.properties.size(),
                    0.0);

                for (size_t property_index = 0; property_index < table.properties.size(); ++property_index)
                {
                    const Property& property = table.properties[property_index];

                    const std::string key = make_property_key(table.element_name, property.name);

                    const RequestedProperty* requested = nullptr;

                    for (const RequestedProperty& candidate : requested_properties)
                    {
                        if (make_property_key(candidate.element_name, candidate.property_name) == key)
                        {
                            requested = &candidate;
                            break;
                        }
                    }

                    if (!requested || !requested->data)
                    {
                        result.ok = false;
                        result.error.message = "PLY property was declared but not read: " + key;
                        return result;
                    }

                    const size_t bytes_per_value = scalar_size(requested->tinyply_type);
                    if (bytes_per_value == 0)
                    {
                        result.ok = false;
                        result.error.message = "Unsupported scalar type for PLY property: " + key;
                        return result;
                    }

                    const uint8_t* raw =
                        reinterpret_cast<const uint8_t*>(requested->data->buffer.get());

                    const size_t expected_bytes =
                        static_cast<size_t>(table.row_count) * bytes_per_value;

                    if (requested->data->buffer.size_bytes() < expected_bytes)
                    {
                        result.ok = false;
                        result.error.message = "PLY property buffer is smaller than expected: " + key;
                        return result;
                    }

                    for (size_t row = 0; row < static_cast<size_t>(table.row_count); ++row)
                    {
                        const uint8_t* value_ptr = raw + row * bytes_per_value;

                        table.values[row * table.properties.size() + property_index] =
                            read_scalar_as_double(value_ptr, requested->tinyply_type);
                    }
                }

                document.scalar_tables.push_back(std::move(table));
            }

            result.ok = true;
            result.document = std::move(document);
            return result;
        }
        catch (const std::exception& e)
        {
            result.ok = false;
            result.error.message = e.what();
            return result;
        }
        catch (...)
        {
            result.ok = false;
            result.error.message = "Unknown error while reading PLY stream";
            return result;
        }
    }

    ReadResult read_ply_file(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            ReadResult result;
            result.ok = false;
            result.error.message = "Failed to open PLY file: " + path.string();
            return result;
        }

        return read_ply_stream(stream);
    }

    ReadResult read_ply_bytes(std::span<const std::uint8_t> bytes)
    {
        // Copy into a string so we can wrap it in an istringstream.
        // The copy is acceptable here — this is a one-shot import path.
        const std::string data(
            reinterpret_cast<const char*>(bytes.data()),
            bytes.size());

        std::istringstream stream(data, std::ios::binary);
        return read_ply_stream(stream);
    }
}