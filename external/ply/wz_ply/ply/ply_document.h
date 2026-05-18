// external/ply/include/ply_document.h
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wz::external::ply
{
    enum class ScalarType
    {
        Int8,
        UInt8,
        Int16,
        UInt16,
        Int32,
        UInt32,
        Float32,
        Float64,
        Unknown,
    };

    struct Property
    {
        std::string name;
        ScalarType type = ScalarType::Unknown;
        bool is_list = false;
    };

    struct Element
    {
        std::string name;
        uint64_t count = 0;
        std::vector<Property> properties;
    };

    struct Header
    {
        std::vector<Element> elements;
    };

    struct ScalarTable
    {
        std::string element_name;
        uint64_t row_count = 0;
        std::vector<Property> properties;

        // Row-major.
        // values[row * properties.size() + property_index]
        std::vector<double> values;
    };

    struct Document
    {
        Header header;
        std::vector<ScalarTable> scalar_tables;
    };

    struct Error
    {
        std::string message;
    };

    struct ReadResult
    {
        bool ok = false;
        Document document;
        Error error;
    };
}