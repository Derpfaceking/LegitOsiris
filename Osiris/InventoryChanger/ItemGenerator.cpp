#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <random>
#include <span>
#include <vector>

#include "../Helpers.h"
#include "Inventory.h"
#include "ItemGenerator.h"

#include "../SDK/ItemSchema.h"
#include "TournamentMatches.h"
#include "GameItems/Lookup.h"

static float generateWear() noexcept
{
    float wear;
    if (const auto condition = Helpers::random(1, 10000); condition <= 1471)
        wear = Helpers::random(0.0f, 0.07f);
    else if (condition <= 3939)
        wear = Helpers::random(0.07f, 0.15f);
    else if (condition <= 8257)
        wear = Helpers::random(0.15f, 0.38f);
    else if (condition <= 9049)
        wear = Helpers::random(0.38f, 0.45f);
    else
        wear = Helpers::random(0.45f, 1.0f);
    return wear;
}

using StaticData::TournamentMap;

[[nodiscard]] static std::array<StickerConfig, 5> generateSouvenirStickers(WeaponId weaponID, std::uint32_t tournamentID, TournamentMap map, TournamentStage stage, TournamentTeam team1, TournamentTeam team2, ProPlayer player) noexcept;

template <typename Integral, std::size_t N>
[[nodiscard]] constexpr auto normalizedFloatsToIntegers(const std::array<float, N>& floats) noexcept
{
    std::array<Integral, N> integers;
    for (std::size_t i = 0; i < N; ++i)
        integers[i] = static_cast<Integral>(floats[i] * (std::numeric_limits<Integral>::max)());
    return integers;
}

class DropRate {
public:
    static constexpr auto MaxDistinctRarities = 6; // in Cobblestone souvenir package, gray to red
    using T = std::uint16_t;

    constexpr DropRate(StaticData::EconRarities rarities, const std::array<float, MaxDistinctRarities>& chances)
        : rarities{ rarities }, chances{ normalizedFloatsToIntegers<T>(chances) } {}

    [[nodiscard]] constexpr EconRarity mapToRarity(T number) const
    {
        for (std::uint8_t i = 0; i < chances.size(); ++i) {
            if (number < chances[i])
                return rarities.getNthRarity(i);
            number -= chances[i];
        }
        return rarities.getNthRarity(0);
    }

    StaticData::EconRarities rarities;
    std::array<T, MaxDistinctRarities> chances{};
};

constexpr auto dropRates = std::to_array<DropRate>({
    { { EconRarity::Blue, EconRarity::Purple, EconRarity::Pink, EconRarity::Red }, { 0.7992f, 0.1598f, 0.032f, 0.0064f } },
    { { EconRarity::Gray, EconRarity::LightBlue, EconRarity::Blue }, { 0.7992f, 0.1598f, 0.041f } } // Inferno souvenir package
});

[[nodiscard]] static EconRarity getRandomRarity(const StaticData::Case& container)
{
    if (const auto rate = std::ranges::find(dropRates, container.rarities, &DropRate::rarities); rate != dropRates.end()) {
        const auto rolledNumber = Helpers::random((std::numeric_limits<DropRate::T>::min)(), (std::numeric_limits<DropRate::T>::max)());
        return rate->mapToRarity(rolledNumber);
    }
    return EconRarity::Default;
}

[[nodiscard]] static std::span<const std::reference_wrapper<const game_items::Item>> getLoot(const StaticData::Case& container)
{
    if (container.rarities.count() > 1) {
        if (const auto rarity = getRandomRarity(container); rarity != EconRarity::Default)
            return StaticData::getCrateLootOfRarity(container, rarity);
    }
    return StaticData::getCrateLoot(container);
}

[[nodiscard]] const game_items::Item& getRandomItemIndexFromContainer(const StaticData::Case& container) noexcept
{
    assert(container.hasLoot());
    std::span<const std::reference_wrapper<const game_items::Item>> loot = getLoot(container);
    assert(!loot.empty());
    return loot[Helpers::random<std::size_t>(0u, loot.size() - 1u)];
}

std::pair<const game_items::Item&, std::size_t> ItemGenerator::generateItemFromContainer(const InventoryItem& caseItem) noexcept
{
    assert(caseItem.isCase());

    const auto& caseData = StaticData::getCase(caseItem.get());
    assert(caseData.hasLoot());

    const auto& unlockedItem = getRandomItemIndexFromContainer(caseData);
    std::size_t dynamicDataIdx = Inventory::InvalidDynamicDataIdx;

    if (caseData.willProduceStatTrak && unlockedItem.isMusic()) {
        DynamicMusicData dynamicData;
        dynamicData.statTrak = 0;
        dynamicDataIdx = Inventory::emplaceDynamicData(std::move(dynamicData));
    } else if (unlockedItem.isSkin()) {
        DynamicSkinData dynamicData;
        const auto& staticData = StaticData::lookup().getStorage().getPaintKit(unlockedItem);
        dynamicData.wear = std::lerp(staticData.wearRemapMin, staticData.wearRemapMax, generateWear());
        dynamicData.seed = Helpers::random(1, 1000);

        if (caseData.isSouvenirPackage()) {
            dynamicData.tournamentID = caseData.souvenirPackageTournamentID;
            const auto& souvenir = Inventory::dynamicSouvenirPackageData(caseItem.getDynamicDataIndex());
            dynamicData.tournamentStage = souvenir.tournamentStage;
            dynamicData.tournamentTeam1 = souvenir.tournamentTeam1;
            dynamicData.tournamentTeam2 = souvenir.tournamentTeam2;
            dynamicData.proPlayer = souvenir.proPlayer;
            dynamicData.stickers = generateSouvenirStickers(unlockedItem.getWeaponID(), caseData.souvenirPackageTournamentID, caseData.tournamentMap, dynamicData.tournamentStage, dynamicData.tournamentTeam1, dynamicData.tournamentTeam2, dynamicData.proPlayer);
        } else if (Helpers::random(0, 9) == 0) {
            dynamicData.statTrak = 0;
        }

        dynamicDataIdx = Inventory::emplaceDynamicData(std::move(dynamicData));
    }

    return std::make_pair(std::cref(unlockedItem), dynamicDataIdx);
}

[[nodiscard]] static std::span<const Match> getTournamentMatches(std::uint32_t tournamentID) noexcept
{
    if (const auto it = std::ranges::lower_bound(tournaments, tournamentID, {}, &Tournament::tournamentID); it != tournaments.end() && it->tournamentID == tournamentID)
        return it->matches;
    assert(false && "Missing tournament match data!");
    return {};
}

constexpr auto operator<=>(TournamentMap a, TournamentMap b) noexcept
{
    return static_cast<std::underlying_type_t<TournamentMap>>(a) <=> static_cast<std::underlying_type_t<TournamentMap>>(b);
}

[[nodiscard]] static std::span<const Match> filterMatchesToMap(std::span<const Match> matches, TournamentMap map) noexcept
{
    if (map == TournamentMap::None)
        return matches;

    assert(std::ranges::is_sorted(matches, {}, &Match::map));

    struct Comp {
        bool operator()(TournamentMap map, const Match& match) const noexcept { return map < match.map; }
        bool operator()(const Match& match, TournamentMap map) const noexcept { return match.map < map; }
    };

    // not using std::ranges::equal_range() here because clang 12 on linux doesn't support it yet
    if (const auto [begin, end] = std::equal_range(matches.begin(), matches.end(), map, Comp{}); begin != end)
        return { begin, end };

    assert(false && "Couldn't find a match played on a map of a souvenir package!");
    return {};
}

[[nodiscard]] static auto generateSouvenirPackageData(const StaticData::Case& caseData) noexcept
{
    assert(caseData.isSouvenirPackage());
    DynamicSouvenirPackageData dynamicData;

    if (const auto matches = getTournamentMatches(caseData.souvenirPackageTournamentID); !matches.empty()) {
        if (const auto matchesOnMap = filterMatchesToMap(matches, caseData.tournamentMap); !matchesOnMap.empty()) {
            const auto& randomMatch = matchesOnMap[Helpers::random(static_cast<std::size_t>(0), matchesOnMap.size() - 1)];
            dynamicData.tournamentStage = randomMatch.stage;
            dynamicData.tournamentTeam1 = randomMatch.team1;
            dynamicData.tournamentTeam2 = randomMatch.team2;
            dynamicData.proPlayer = randomMatch.getRandomMVP();
        }
    }

    return dynamicData;
}

[[nodiscard]] std::time_t tmToUTCTimestamp(std::tm& tm) noexcept
{
#ifdef _WIN32
    return _mkgmtime(&tm);
#else
    return timegm(&tm);
#endif
}

[[nodiscard]] std::time_t getStartOfYearTimestamp(std::uint16_t year) noexcept
{
    assert(year >= 1900);
    std::tm tm{};
    tm.tm_mday = 1;
    tm.tm_year = year - 1900;
    return tmToUTCTimestamp(tm);
}

[[nodiscard]] std::time_t getEndOfYearTimestamp(std::uint16_t year) noexcept
{
    assert(year >= 1900);
    std::tm tm{};
    tm.tm_sec = 59;
    tm.tm_min = 59;
    tm.tm_hour = 23;
    tm.tm_mday = 31;
    tm.tm_mon = 12 - 1;
    tm.tm_year = year - 1900;
    return tmToUTCTimestamp(tm);
}

[[nodiscard]] static std::pair<std::time_t, std::time_t> clampTimespanToNow(std::time_t min, std::time_t max) noexcept
{
    const auto now = std::time(nullptr);
    return std::make_pair((std::min)(min, now), (std::min)(max, now));
}

[[nodiscard]] static std::uint32_t getRandomDateTimestampOfYear(std::uint16_t year) noexcept
{
    const auto [min, max] = clampTimespanToNow(getStartOfYearTimestamp(year), getEndOfYearTimestamp(year));
    return static_cast<std::uint32_t>(Helpers::random(min, max));
}

std::size_t ItemGenerator::createDefaultDynamicData(const game_items::Item& item) noexcept
{
    std::size_t index = Inventory::InvalidDynamicDataIdx;

    if (item.isSkin()) {
        const auto& staticData = StaticData::lookup().getStorage().getPaintKit(item);
        DynamicSkinData dynamicData;
        dynamicData.wear = std::lerp(staticData.wearRemapMin, staticData.wearRemapMax, Helpers::random(0.0f, 0.07f));
        dynamicData.seed = Helpers::random(1, 1000);

        if (Helpers::isMP5LabRats(item.getWeaponID(), StaticData::lookup().getStorage().getPaintKit(item).id))
            dynamicData.stickers[3].stickerID = 28;

        index = Inventory::emplaceDynamicData(std::move(dynamicData));
    } else if (item.isGloves()) {
        const auto& staticData = StaticData::lookup().getStorage().getPaintKit(item);
        DynamicGloveData dynamicData;
        dynamicData.wear = std::lerp(staticData.wearRemapMin, staticData.wearRemapMax, Helpers::random(0.0f, 0.07f));
        dynamicData.seed = Helpers::random(1, 1000);
        index = Inventory::emplaceDynamicData(std::move(dynamicData));
    } else if (item.isAgent()) {
        index = Inventory::emplaceDynamicData(DynamicAgentData{});
    } else if (item.isMusic()) {
        index = Inventory::emplaceDynamicData(DynamicMusicData{});
    } else if (item.isCase()) {
        if (const auto& staticData = StaticData::getCase(item); staticData.isSouvenirPackage())
            index = Inventory::emplaceDynamicData(generateSouvenirPackageData(staticData));
    } else if (item.isServiceMedal()) {
        DynamicServiceMedalData dynamicData;
        dynamicData.issueDateTimestamp = getRandomDateTimestampOfYear(StaticData::lookup().getStorage().getServiceMedalYear(item));
        index = Inventory::emplaceDynamicData(std::move(dynamicData));
    } else if (item.isTournamentCoin()) {
        index = Inventory::emplaceDynamicData(DynamicTournamentCoinData{});
    }

    return index;
}

[[nodiscard]] static const Match* findTournamentMatch(std::uint32_t tournamentID, TournamentMap map, TournamentStage stage, TournamentTeam team1, TournamentTeam team2) noexcept
{
    const auto matchesOnMap = filterMatchesToMap(getTournamentMatches(tournamentID), map);
    if (matchesOnMap.empty())
        return nullptr;

    const auto match = std::ranges::find_if(matchesOnMap, [stage, team1, team2](const auto& match) { return match.stage == stage && match.team1 == team1 && match.team2 == team2; });
    return (match != matchesOnMap.end() ? &*match : nullptr);
}

[[nodiscard]] static std::uint8_t getNumberOfSupportedStickerSlots(WeaponId weaponID) noexcept
{
    if (const auto def = memory->itemSystem()->getItemSchema()->getItemDefinitionInterface(weaponID))
        return static_cast<std::uint8_t>(std::clamp(def->getNumberOfSupportedStickerSlots(), 0, 5));
    return 0;
}

[[nodiscard]] static std::array<StickerConfig, 5> generateSouvenirStickers(WeaponId weaponID, std::uint32_t tournamentID, TournamentMap map, TournamentStage stage, TournamentTeam team1, TournamentTeam team2, ProPlayer player) noexcept
{
    std::array<StickerConfig, 5> stickers;

    stickers[0].stickerID = StaticData::lookup().findTournamentEventStickerID(tournamentID);

    if (tournamentID != 1) {
        stickers[1].stickerID = StaticData::lookup().findTournamentTeamGoldStickerID(tournamentID, team1);
        stickers[2].stickerID = StaticData::lookup().findTournamentTeamGoldStickerID(tournamentID, team2);

        if (const auto match = findTournamentMatch(tournamentID, map, stage, team1, team2); match && match->hasMVPs())
            stickers[3].stickerID = StaticData::lookup().findTournamentPlayerGoldStickerID(tournamentID, static_cast<int>(player));
        else if (tournamentID >= 18) // starting with PGL Stockholm 2021
            stickers[3].stickerID = StaticData::getTournamentMapGoldStickerID(map);
    }

    std::mt19937 gen{ std::random_device{}() };
    std::shuffle(stickers.begin(), stickers.begin() + getNumberOfSupportedStickerSlots(weaponID), gen);

    return stickers;
}
