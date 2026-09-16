#include "PCH.h"

#include "Core/ClimateMap.h"

#include "Settings.h"

#include <fstream>

namespace RSL::ClimateMap
{
    namespace
    {
        constexpr float CELL = 4096.0f;
        constexpr int   SIDE = 33;              // Skyrim's own height grid
        constexpr int   PER_CELL = SIDE * SIDE;
        constexpr float SPACING = CELL / (SIDE - 1);   // 128 units

        // Next to the plugin, because that is where the mod's own files live
        // and the game's working directory is the game folder.
        constexpr const char* PATH = "Data/SKSE/Plugins/_RSL_Climate.bin";

        // What the baker writes in each block, to say which worldspace it is.
        constexpr const char* TAGS[] = { "TAMRIEL", "SOLSTH" };
        constexpr std::size_t WORLDS = std::size(TAGS);

        struct Grid
        {
            std::int32_t              minX{ 0 };
            std::int32_t              minY{ 0 };
            std::int32_t              width{ 0 };
            std::int32_t              height{ 0 };
            std::vector<std::int32_t> index;   // cell slot, or -1
            std::vector<std::int16_t> data;    // hundredths of a degree
            bool                      ready{ false };
        };

        std::array<Grid, WORLDS> grids;
        bool                     loaded = false;

        template <class T>
        bool ReadRaw(std::ifstream& a_in, T& a_out)
        {
            a_in.read(reinterpret_cast<char*>(&a_out), sizeof(T));
            return static_cast<bool>(a_in);
        }
    }

    bool Load()
    {
        grids = {};
        loaded = false;

        std::ifstream in(PATH, std::ios::binary);
        if (!in) {
            logger::warn("climate map: {} is not there - falling back to regions",
                PATH);
            return false;
        }

        char          magic[8]{};
        std::uint32_t version = 0;
        std::uint32_t blocks = 0;
        if (!ReadRaw(in, magic) || !ReadRaw(in, version) || !ReadRaw(in, blocks)) {
            logger::error("climate map: {} has no header", PATH);
            return false;
        }
        if (std::memcmp(magic, "RSLCLIM2", 8) != 0 || version != 2) {
            logger::error("climate map: {} is not a version this build reads "
                          "- rerun tools/bake_climate.py",
                PATH);
            return false;
        }
        if (blocks == 0 || blocks > 8) {
            logger::error("climate map: {} blocks is not a number of worldspaces",
                blocks);
            return false;
        }

        for (std::uint32_t b = 0; b < blocks; ++b) {
            char          tag[8]{};
            std::int32_t  minX = 0, minY = 0, width = 0, height = 0;
            std::uint32_t cells = 0;
            if (!ReadRaw(in, tag) || !ReadRaw(in, minX) || !ReadRaw(in, minY) ||
                !ReadRaw(in, width) || !ReadRaw(in, height) || !ReadRaw(in, cells)) {
                logger::error("climate map: block {} ends in the middle", b);
                return false;
            }
            if (width <= 0 || height <= 0 || cells == 0) {
                logger::error("climate map: block {} says {}x{} cells over {} slots",
                    b, width, height, cells);
                return false;
            }

            const auto squares = static_cast<std::size_t>(width) * height;
            std::vector<std::int32_t> index(squares);
            in.read(reinterpret_cast<char*>(index.data()),
                static_cast<std::streamsize>(squares * sizeof(std::int32_t)));
            std::vector<std::int16_t> data(
                static_cast<std::size_t>(cells) * PER_CELL);
            in.read(reinterpret_cast<char*>(data.data()),
                static_cast<std::streamsize>(data.size() * sizeof(std::int16_t)));
            if (!in) {
                logger::error("climate map: the file ends before block {} does", b);
                return false;
            }

            // Every slot has to be one we can actually read from, or a bad file
            // walks off the end of the vector at the worst possible moment.
            for (const std::int32_t slot : index) {
                if (slot >= 0 && static_cast<std::uint32_t>(slot) >= cells) {
                    logger::error("climate map: block {} points at slot {} of {}",
                        b, slot, cells);
                    return false;
                }
            }

            std::size_t which = WORLDS;
            for (std::size_t w = 0; w < WORLDS; ++w) {
                if (std::strncmp(tag, TAGS[w], 8) == 0) {
                    which = w;
                    break;
                }
            }
            if (which == WORLDS) {
                logger::warn("climate map: block {} is tagged '{}', which this "
                             "build has no worldspace for - skipped",
                    b, std::string(tag, strnlen(tag, 8)));
                continue;
            }

            Grid& grid = grids[which];
            grid.minX = minX;
            grid.minY = minY;
            grid.width = width;
            grid.height = height;
            grid.index = std::move(index);
            grid.data = std::move(data);
            grid.ready = true;
            loaded = true;

            logger::info("climate map: {} - {} cells over a {}x{} grid from "
                         "({}, {}), {:.1f} MB",
                TAGS[which], cells, width, height, minX, minY,
                (grid.data.size() * sizeof(std::int16_t)
                    + grid.index.size() * sizeof(std::int32_t))
                    / 1048576.0);
        }

        return loaded;
    }

    bool Ready()
    {
        return loaded;
    }

    std::optional<float> Surface(World a_world, float a_x, float a_y)
    {
        const auto which = static_cast<std::size_t>(a_world);
        if (which >= WORLDS || !grids[which].ready) {
            return std::nullopt;
        }
        const Grid& grid = grids[which];

        const auto gx = static_cast<std::int32_t>(std::floor(a_x / CELL));
        const auto gy = static_cast<std::int32_t>(std::floor(a_y / CELL));
        const std::int32_t ix = gx - grid.minX;
        const std::int32_t iy = gy - grid.minY;
        if (ix < 0 || iy < 0 || ix >= grid.width || iy >= grid.height) {
            return std::nullopt;
        }

        const std::int32_t slot =
            grid.index[static_cast<std::size_t>(iy) * grid.width + ix];
        if (slot < 0) {
            return std::nullopt;
        }
        const std::int16_t* cell =
            grid.data.data() + static_cast<std::size_t>(slot) * PER_CELL;

        // Where in the cell, in grid squares.
        const float fx = (a_x - gx * CELL) / SPACING;
        const float fy = (a_y - gy * CELL) / SPACING;
        const int   i = std::clamp(static_cast<int>(fx), 0, SIDE - 2);
        const int   j = std::clamp(static_cast<int>(fy), 0, SIDE - 2);
        const float u = fx - static_cast<float>(i);
        const float v = fy - static_cast<float>(j);

        const float t00 = cell[j * SIDE + i] * 0.01f;
        const float t10 = cell[j * SIDE + i + 1] * 0.01f;
        const float t01 = cell[(j + 1) * SIDE + i] * 0.01f;
        const float t11 = cell[(j + 1) * SIDE + i + 1] * 0.01f;

        // The terrain is two triangles per square, not a curved patch, so the
        // answer is barycentric within whichever triangle the point fell in.
        // Averaging over the square instead would cut the corner of the one
        // being stood on, which is exactly where a ridge line is.
        return (u + v <= 1.0f)
                   ? t00 + (t10 - t00) * u + (t01 - t00) * v
                   : t11 + (t01 - t11) * (1.0f - u) + (t10 - t11) * (1.0f - v);
    }

    float WithAltitude(float a_surface, float a_ground, float a_z)
    {
        const float above = a_z - a_ground;
        const float span = std::max(1.0f, Settings::fSkySpan);
        const float share =
            std::clamp((above - Settings::fSkyBuffer) / span, 0.0f, 1.0f);
        if (share <= 0.0f) {
            return a_surface;
        }
        return a_surface * (1.0f - share) + Settings::fSkyTemp * share;
    }
}
