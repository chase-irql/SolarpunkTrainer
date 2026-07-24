#include "ResourceNames.h"

#include <array>

namespace {

    struct NameMapping {
        std::string_view RawClassName;
        std::string_view DisplayName;
    };

    // Concrete classes observed in resource_marker_classes.tsv. Keep explicit
    // mappings here so display names remain intentional; unknown future
    // classes fall back to their raw SDK name.
    constexpr std::array<NameMapping, 57> NameMappings{ {
        { "BP_Animal_Chicken_C", "Chicken" },
        { "BP_Animal_Pig_C", "Pig" },
        { "BP_Animal_Sheep_C", "Sheep" },
        { "BP_Clay_GrabItem_C", "Clay" },
        { "BP_LocalPlant_Algae_C", "Algae" },
        { "BP_LocalPlant_Carrot_C", "Carrot" },
        { "BP_LocalPlant_Corn_C", "Corn" },
        { "BP_LocalPlant_Cotton_C", "Cotton" },
        { "BP_LocalPlant_Paprika_C", "Paprika" },
        { "BP_LocalPlant_Raspberry_C", "Raspberry" },
        { "BP_LocalPlant_Sunflower_C", "Sunflower" },
        { "BP_LocalPlant_Tomato_C", "Tomato" },
        { "BP_LocalPlant_Watermelon_C", "Watermelon" },
        { "BP_LocalPlant_Wheat_C", "Wheat" },
        { "BP_Merchant_C", "Merchant" },
        { "BP_Ore_Cobalt_C", "Cobalt" },
        { "BP_Ore_Clay_C", "Clay" },
        { "BP_Ore_Copper_C", "Copper" },
        { "BP_Ore_Diamond_C", "Diamond" },
        { "BP_Ore_Iron_C", "Iron" },
        { "BP_Ore_Quartz_C", "Quartz" },
        { "BP_Ore_Sandstone_C", "Sandstone" },
        { "BP_Ore_Stone_1_C", "Stone" },
        { "BP_Ore_Stone_10_C", "Stone" },
        { "BP_Ore_Stone_11_C", "Stone" },
        { "BP_Ore_Stone_12_C", "Stone" },
        { "BP_Ore_Stone_13_C", "Stone" },
        { "BP_Ore_Stone_14_C", "Stone" },
        { "BP_Ore_Stone_16_C", "Stone" },
        { "BP_Ore_Stone_2_C", "Stone" },
        { "BP_Ore_Stone_3_C", "Stone" },
        { "BP_Ore_Stone_6_C", "Stone" },
        { "BP_Ore_Stone_7_C", "Stone" },
        { "BP_Ore_Stone_8_C", "Stone" },
        { "BP_Ore_Stone_9_C", "Stone" },
        { "BP_Orepatch_Clay_C", "Clay Deposit" },
        { "BP_Orepatch_Cobalt_C", "Cobalt Deposit" },
        { "BP_Orepatch_Copper_C", "Copper Deposit" },
        { "BP_Orepatch_Diamond_C", "Diamond Deposit" },
        { "BP_Orepatch_Iron_C", "Iron Deposit" },
        { "BP_Orepatch_Quartz_C", "Quartz Deposit" },
        { "BP_Orepatch_Sandstone_C", "Sandstone Deposit" },
        { "BP_Orepatch_Stone_C", "Stone Deposit" },
        { "BP_Plant_Raspberrybush_C", "Raspberry Bush" },
        { "BP_RandomLootchest_Phase2_C", "Loot Chest - Phase 2" },
        { "BP_RandomLootchest_Phase3_C", "Loot Chest - Phase 3" },
        { "BP_RandomLootchest_Phase4_C", "Loot Chest - Phase 4" },
        { "BP_RandomLootchest_Phase5_C", "Loot Chest - Phase 5" },
        { "BP_RandomLootchest_Phase6_C", "Loot Chest - Phase 6" },
        { "BP_Stick_GrabItem_C", "Stick" },
        { "BP_Stone_GrabItem_C", "Stone" },
        { "BP_Tradebot_ENERGY_1_C", "Energy Tradebot" },
        { "BP_Tree_Alder_C", "Alder Tree" },
        { "BP_Tree_Birch_C", "Birch Tree" },
        { "BP_Tree_Maple_C", "Maple Tree" },
        { "BP_Tree_Oak_C", "Oak Tree" },
        { "BP_Tree_Pine_C", "Pine Tree" }
    } };

} // namespace

std::string_view Solarpunk::ResourceNames::Resolve(
    std::string_view rawClassName) {
    for (const NameMapping& mapping : NameMappings) {
        if (mapping.RawClassName == rawClassName)
            return mapping.DisplayName;
    }
    return rawClassName;
}

bool Solarpunk::ResourceNames::HasFriendlyName(
    std::string_view rawClassName) {
    for (const NameMapping& mapping : NameMappings) {
        if (mapping.RawClassName == rawClassName)
            return true;
    }
    return false;
}

size_t Solarpunk::ResourceNames::MappingCount() {
    return NameMappings.size();
}
