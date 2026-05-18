// external/ply/src/ply_reader.cpp

#include <ply/ply_reader.h>

#include "../tinyply/tinyply.h"

#include <cstdint>
#include <cstring>
#include <fstream>
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

    ReadResult read_ply_file(const std::filesystem::path& path)
    {
        ReadResult result;

        try
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
            {
                result.ok = false;
                result.error.message = "Failed to open PLY file: " + path.string();
                return result;
            }

            tinyply::PlyFile file;
            file.parse_header(stream);

            Document document;

            std::vector<RequestedProperty> requested_properties;

            const std::vector<tinyply::PlyElement> elements = file.get_elements();

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
            result.error.message = "Unknown error while reading PLY file";
            return result;
        }
    }
}