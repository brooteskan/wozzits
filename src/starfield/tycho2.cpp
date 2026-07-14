// src/starfield/tycho2.cpp

#include <starfield/tycho2.h>

#include <cstdlib>
#include <string>

namespace wz::engine::starfield
{
    namespace
    {
        // A 1-indexed [start, len] fixed-width slice, clamped to the line.
        std::string_view slice(std::string_view line, std::size_t start1, std::size_t len)
        {
            const std::size_t start0 = start1 - 1;
            if (start0 >= line.size()) {
                return {};
            }
            return line.substr(start0, std::min(len, line.size() - start0));
        }

        // Parse a fixed-width numeric field; blank (all spaces) -> nullopt.
        std::optional<double> field_number(std::string_view s)
        {
            const std::size_t b = s.find_first_not_of(" \t\r\n");
            if (b == std::string_view::npos) {
                return std::nullopt;
            }
            const std::size_t e = s.find_last_not_of(" \t\r\n");
            const std::string token(s.substr(b, e - b + 1));
            char* end = nullptr;
            const double value = std::strtod(token.c_str(), &end);
            if (end == token.c_str()) {
                return std::nullopt;   // no digits consumed
            }
            return value;
        }
    } // namespace

    std::optional<CatalogRecord> parse_tycho2_line(std::string_view line)
    {
        const char pflag = (line.size() >= 14) ? line[13] : ' ';

        // Prefer the mean ICRS position; fall back to the observed position when
        // the record has no mean position ('X') or the field is blank.
        std::optional<double> ra_deg = field_number(slice(line, 16, 12));
        std::optional<double> de_deg = field_number(slice(line, 29, 12));
        if (pflag == 'X' || !ra_deg || !de_deg) {
            ra_deg = field_number(slice(line, 153, 12));
            de_deg = field_number(slice(line, 166, 12));
        }
        if (!ra_deg || !de_deg) {
            return std::nullopt;
        }

        const std::optional<double> bt = field_number(slice(line, 111, 6));
        const std::optional<double> vt = field_number(slice(line, 124, 6));

        CatalogRecord record;
        record.ra_hours = *ra_deg / 15.0;   // degrees -> hours
        record.dec_deg = *de_deg;

        if (vt && bt) {
            record.vmag = *vt - 0.090 * (*bt - *vt);
            record.bv = 0.850 * (*bt - *vt);
            record.has_bv = true;
        } else if (vt) {
            record.vmag = *vt;
            record.bv = 0.0;
            record.has_bv = false;
        } else if (bt) {
            record.vmag = *bt;   // rough: only BT available
            record.bv = 0.0;
            record.has_bv = false;
        } else {
            return std::nullopt;   // no magnitude at all
        }

        return record;
    }

} // namespace wz::engine::starfield
