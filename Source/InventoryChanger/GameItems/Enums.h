#pragma once

#include <cstdint>
#include <type_traits>

namespace game_items
{

enum class TournamentMap : std::uint8_t {
    None = 0,
    Ancient,
    Cache,
    Cobblestone,
    Dust2,
    Inferno,
    Mirage,
    Nuke,
    Overpass,
    Train,
    Vertigo
};

constexpr auto operator<=>(TournamentMap a, TournamentMap b) noexcept
{
    return static_cast<std::underlying_type_t<TournamentMap>>(a) <=> static_cast<std::underlying_type_t<TournamentMap>>(b);
}

}
