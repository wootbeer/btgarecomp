// What the Cheats tab does in the game (include/btga_cheats.h).
//
// Stock codes are the game's own: the code screen (func_800D0070 matches the
// text against the table at 0x80121C3C) flips a flag, and the toggle sets the
// same flag. While a toggle is on the flag is kept set; turning it off clears
// it once, so typing the code in the game still works with the toggle off.
//
// GameShark and Action Replay codes are the community's codes for this ROM.
// The ones that write to the game's memory are applied once a frame here, as
// the cheat device did (codes 80, 81, D0-D3 and 50). Those on fixed tables in
// the game's data put the original bytes back when turned off.
//
// The rest change the game's code (an instruction, or a jump table entry).
// The recompiled code doesn't read its instructions from memory, so writing
// them does nothing; instead each one is a hook in battletanxga.us.rev0.toml
// that asks btga_cheat() which version of the instruction to run.
#include <atomic>
#include <cstdlib>
#include <iterator>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "btga_cheats.h"
#include "recomp.h"

namespace {
    using btga::cheats::Cheat;
    using btga::cheats::Choice;
    using btga::cheats::Section;

    // Read by the hooks in battletanxga.us.rev0.toml through btga_cheat();
    // the numbers are used there.
    enum Hook {
        NoHook = -1,
        EverybodyInvisible = 0,  // func_80096424 0x80096554
        WeaponTypesChange = 1,   // func_8008C5D8 0x8008C5E8
        ShellsLessDamage = 2,    // func_8008C5D8 0x8008C808
        GunFireGroundLevel = 3,  // func_8008C3B4 0x8008C544
        RapidFire = 4,           // func_8008E780 0x8008E7F0
        StingerWeapons = 5,      // func_8008BF5C 0x8008C394
        MoveAfterLevel = 6,      // func_8009C524 0x8009C624
        ChainReactionPumps = 7,  // func_800A5744 0x800A5774
        EnemiesCantMove = 8,     // func_80082834 0x80082854
        EnemiesTurntable = 9,    // func_8009D75C 0x8009D7AC
        JamRadar = 10,           // func_800C7AA0 jump tables at 0x800C7B00 and 0x800C7BCC
        PickupMines = 11,        // func_8008ECB4 jump table at 0x8008ECF0, btga_cheat_pickup()
        PickupHealth = 12,
        PickupStar = 13,
        PickupAmmo = 14,
        PickupFlamethrower = 15,
        PickupGrenades = 16,
        PickupRadar = 17,
        KillCounterFrozen = 18,  // func_800A89B0 0x800A89F8
        GuidedForever = 19,      // func_800DB850 0x800DB8C4
        GunBuddiesInvuln = 20,   // func_800E4530 0x800E459C
        TankBulletsInfinite = 21, // func_8008E780 0x8008E97C
        TankBucksInfinite = 22,  // func_800D6EEC 0x800D7420
        UnlimitedLaser = 23,     // func_800DB850 0x800DC14C
        MaxTimeTankWars = 24,    // func_8009A95C 0x8009A9C4
        SpecialAmmoInfinite = 25, // func_8008C5D8 / func_8008E780, 11 stores
        HookCount
    };

    struct Entry {
        Cheat cheat;
        // Stock: the game's flag (1 or 2 bytes).
        uint32_t flag_addr = 0;
        int flag_size = 0;
        // Memory codes, one list per choice after "off" (one list for a toggle).
        std::vector<std::vector<const char*>> codes = {};
        // Put the original bytes back when turned off.
        bool restore = false;
        Hook hook = NoHook;
    };

    Entry stock(const char* id, const char* name, const char* code, const char* text, uint32_t addr, int size = 1) {
        std::string description = std::string(text) + (text[0] ? "<br/><br/>" : "") + "Code: <b>" + code + "</b>";
        return { { id, name, description, Section::Stock, {} }, addr, size };
    }

    std::string code_text(const char* device, const std::vector<const char*>& codes) {
        std::string ret = std::string(device) + (codes.size() > 2 ? " code:<br/><b>" : " code: <b>");
        for (size_t i = 0; i < codes.size(); i++) {
            ret += (i ? (i % 2 ? " " : "<br/>") : "") + std::string(codes[i]);
        }
        return ret + "</b>";
    }

    Entry gs(const char* id, const char* name, const char* text, std::vector<const char*> codes, bool restore = true) {
        std::string description = std::string(text) + (text[0] ? "<br/><br/>" : "") + code_text("GameShark", codes);
        return { { id, name, description, Section::GameShark, {} }, 0, 0, { codes }, restore };
    }

    // A code that changes the game's code, done with a hook.
    Entry gs_hook(const char* id, const char* name, const char* text, const char* code, Hook hook) {
        std::string description = std::string(text) + (text[0] ? "<br/><br/>" : "") + "GameShark code: <b>" + code + "</b>";
        return { { id, name, description, Section::GameShark, {} }, 0, 0, {}, false, hook };
    }

    Entry ar_hook(const char* id, const char* name, const char* text, std::vector<const char*> codes, Hook hook) {
        std::string description = std::string(text) + (text[0] ? "<br/><br/>" : "") + code_text("Action Replay", codes);
        return { { id, name, description, Section::ActionReplay, {} }, 0, 0, {}, false, hook };
    }

    // The pickup jump table's cases, in the order of the code list. The value
    // is the case's index in the table (the item type minus one).
    struct Item {
        const char* key;
        const char* name;
        int index;
    };
    const Item pickup_items[] = {
        { "Swarmers", "Swarmers", 0 },
        { "Lasers", "Lasers", 1 },
        { "GuidedMissiles", "Guided Missiles", 2 },
        { "Nuke", "Nuke", 4 },
        { "Mines", "Mines", 5 },
        { "Turbo", "Turbo", 11 },
        { "GunBuddies", "Gun Buddies", 3 },
        { "BouncingBetties", "Bouncing Betties", 13 },
        { "Flamethrower", "Flamethrower", 14 },
        { "Cloaking", "Cloaking", 15 },
        { "Grenades", "Grenades", 16 },
        { "TankBucks", "Tank Bucks", 17 },
        { "Radar", "Radar", 18 },
        { "Health", "Health", 6 },
        { "Teleporters", "Teleporters", 8 },
        { "PlasmaBolts", "Plasma Bolts", 9 },
        { "Shields", "Shields", 12 },
        { "Ammo", "Ammo", 10 },
        { "Star", "Star", 7 },
    };

    // A pickup modifier: what picking up this item gives instead (the code
    // rewrote the item's jump table entry; btga_cheat_pickup does it here).
    Entry pickup(const char* id, const char* name, const char* address, Hook hook) {
        std::vector<Choice> choices = { { "Normal", "Normal" } };
        for (const Item& item : pickup_items) {
            choices.push_back({ item.key, item.name });
        }
        std::string description = std::string("What picking this item up gives instead.<br/><br/>GameShark code: <b>") +
            address + " XXXX</b>, XXXX picking the item.";
        return { { id, name, description, Section::GameShark, choices }, 0, 0, {}, false, hook };
    }

    const char* const special_ammo_codes[] = {
        "8108D230", "2400", "8108D990", "2400", "8108DB20", "2400", "8108DD00", "2400",
        "8108DE24", "2400", "8108DEF8", "2400", "8108E174", "2400", "8108E3E0", "2400",
        "8108E4C0", "2400", "8108E518", "2400", "8108EA84", "2400",
    };

    // Player n's all special weapons and infinite ammo, by mode.
    Entry player_weapons(int player, const char* battlelord, const char* deathmatch, const char* hold_em) {
        std::string id = "p" + std::to_string(player) + "_special_weapons";
        std::string name = "Player " + std::to_string(player) + " Special Weapons";
        Entry e = {
            { id, name,
              "Player " + std::to_string(player) + " has all special weapons and infinite ammo. "
              "Each mode has its own code: pick the mode you're playing."
              "<br/><br/>Action Replay code: <b>50000E02 0000<br/>81xxxxxx FFFF</b>",
              Section::ActionReplay,
              { { "Off", "Off" }, { "Battlelord", "Battlelord" }, { "Deathmatch", "Deathmatch, Frenzy, Convoy" }, { "HoldEm", "Hold 'Em" } } },
        };
        e.codes = {
            { "50000E02", "0000", battlelord, "FFFF" },
            { "50000E02", "0000", deathmatch, "FFFF" },
            { "50000E02", "0000", hold_em, "FFFF" },
        };
        return e;
    }

    std::vector<Entry> make_entries() {
        std::vector<Entry> e;

        // Stock codes, in the order of the game's table. Flags from func_800D0070.
        e.push_back(stock("custom_gangs", "Custom Gangs", "TRDDYBRRKS", "", 0x80121CC0, 2));
        e.push_back(stock("level_select", "Level Select", "80DYS", "", 0x80117F48, 2));
        e.push_back(stock("invincibility", "Invincibility", "HPPYHPPY", "", 0x80125AB0));
        e.push_back(stock("all_weapons", "All Weapons", "RCKTSRDGLR", "", 0x80125AB1));
        e.push_back(stock("extra_level", "Extra Level in Campaign", "WRDRB", "", 0x80125AB2));
        e.push_back(stock("scramble_models", "Scramble Models", "PRSSTNCFTM", "", 0x80125AB3));
        e.push_back(stock("teddy_bear_flags", "Teddy Bear Flags", "BRNCSSTS", "All the flags show up as teddy bears.", 0x80125AB4));
        e.push_back(stock("cassandra_gang", "Cassandra's Gang", "DRMWVR", "", 0x80125AB5));
        e.push_back(stock("brandon_gang", "Brandon's Gang", "NNKNHCKS", "", 0x80125AB6));
        e.push_back(stock("mini_tanks_1", "Mini Tanks (Team 1)", "DR1NKM3", "", 0x80125AB8));
        e.push_back(stock("mini_tanks_2", "Mini Tanks (Team 2)", "SHR1MP", "", 0x80125AB9));
        e.push_back(stock("mini_tanks_3", "Mini Tanks (Team 3)", "T0MTHMB", "", 0x80125ABA));
        e.push_back(stock("mini_tanks_4", "Mini Tanks (Team 4)", "M1N1M3", "", 0x80125ABB));
        e.push_back(stock("mini_tanks_5", "Mini Tanks (Team 5)", "PYGMY", "", 0x80125ABC));

        // GameShark. Left out: the ones that set the stock codes' flags (the
        // toggles above), and Levitate, which needs the GameShark's button.
        e.push_back(gs("gs_tank_bucks", "Infinite Tank Bucks", "", { "80236A9F", "0064" }, false));
        e.push_back(gs("gs_less_gravity", "Less Gravity", "", { "8007317D", "0080" }));
        e.push_back(gs("gs_no_enemies", "Turn Off Enemies", "", { "80114CC1", "0000" }));
        e.push_back(gs("gs_no_buildings", "Turn Off Buildings", "", { "80114CC0", "0000" }));
        e.push_back(gs("gs_no_submarines", "Turn Off Submarines", "", { "80114CCF", "0000" }));
        e.push_back(gs("gs_no_explosives", "Turn Off Explosive Devices", "Like explosive barrels.", { "80114CE8", "0000" }));
        e.push_back(gs("gs_keep_debris", "Leave Debris Behind", "Turns off the item debris cleaner.", { "80114CF4", "0000" }));
        e.push_back(gs("gs_move_in_menu", "Move While Menu Is Up", "", { "80114CFD", "0001" }));
        e.push_back(gs_hook("gs_invisible", "Everybody Is Invisible", "", "80096554 0001", EverybodyInvisible));
        e.push_back(gs("gs_miniature_vision", "Miniature World Vision", "", { "800F93C6", "0001" }));
        e.push_back(gs("gs_granite_world", "Granite World", "", { "80116772", "0020" }));
        e.push_back(gs_hook("gs_weapon_types", "All Weapon Types Change", "Mines become Nukes, Gun Buddies become The Edge, and so on.",
            "8008C5E9 0009", WeaponTypesChange));
        e.push_back(gs_hook("gs_weak_shells", "Tank Shells Do Less Damage", "", "8008C808 0020", ShellsLessDamage));
        {
            Entry tanker = gs("gs_tanker_explosions", "Tanker/Gas Pump Explosions", "", {});
            tanker.cheat.description = "GameShark code: <b>80072244 0040</b> (more deadly) or <b>80072244 003E</b> (little damage)";
            tanker.cheat.choices = { { "Normal", "Normal" }, { "MoreDeadly", "More Deadly" }, { "LittleDamage", "Little Damage" } };
            tanker.codes = { { "80072244", "0040" }, { "80072244", "003E" } };
            e.push_back(tanker);
        }
        {
            Entry rockets = gs("gs_rocket_speed", "Rocket Speed", "", {});
            rockets.cheat.description = "GameShark code: <b>80071AA4 0042</b> (faster) or <b>80071AA4 0040</b> (slower)";
            rockets.cheat.choices = { { "Normal", "Normal" }, { "Faster", "Faster" }, { "Slower", "Slower" } };
            rockets.codes = { { "80071AA4", "0042" }, { "80071AA4", "0040" } };
            e.push_back(rockets);
        }
        e.push_back(gs_hook("gs_ground_fire", "Gun Fire At Ground Level", "", "8008C544 0001", GunFireGroundLevel));
        e.push_back(gs("gs_edge_power", "Full Edge Power All Levels", "",
            { "50001370", "0000", "81124BDE", "07D0", "50001370", "0000", "81124BE0", "0A0A" }));
        e.push_back(gs("gs_enemies_switch", "Enemies Switch To Your Side", "Numerous enemies switch to your side, on all levels.",
            { "50001370", "0000", "81124B90", "0000" }));
        e.push_back(gs_hook("gs_jam_radar", "Jam Radar", "", "50000504 0000<br/>81073D0A 7BEC<br/>50000704 0000<br/>81073D22 7BEC", JamRadar));
        e.push_back(gs("gs_all_tanks", "All Tanks All Levels", "Every tank, in every level of every campaign.",
            { "50001370", "0000", "81124BD6", "FFFF", "50001370", "0000", "81124BDA", "FFFF" }));
        e.push_back(gs_hook("gs_rapid_fire", "Rapid Fire", "Might only fire so many times each level.", "8008E7F0 0001", RapidFire));
        e.push_back(gs("gs_max_visibility", "Maximum Visibility", "", { "800F9522", "0001" }));
        e.push_back(gs_hook("gs_stinger", "Stinger Weapons", "Weapons now sting, better move fast.", "8008C394 0001", StingerWeapons));
        e.push_back(gs_hook("gs_move_after_level", "Move After Beating A Level", "Press <b>B</b> to end the level.", "8009C624 0001", MoveAfterLevel));
        e.push_back(gs_hook("gs_chain_pumps", "Chain Reaction Pumps",
            "If one tanker or gas pump blows up, they all do. (Shoot a pump in the Arizona level and a message from the staff appears.)",
            "800A5774 0001", ChainReactionPumps));
        {
            Entry edge = gs("gs_edge_area", "Edge Power Area", "", {});
            edge.cheat.description = "GameShark code: <b>80071AF0 0043</b> (hits about everything on the map) or <b>80071AF0 0044</b> (a large area)";
            edge.cheat.choices = { { "Normal", "Normal" }, { "Everything", "About Everything" }, { "Large", "Large Area" } };
            edge.codes = { { "80071AF0", "0043" }, { "80071AF0", "0044" } };
            e.push_back(edge);
        }
        {
            Entry lasers = gs("gs_laser_damage", "Laser Damage", "", {});
            lasers.cheat.description = "GameShark code: <b>80071AB4 0043</b> (massive) or <b>80071AB4 0040</b> (lasers don't do jack)";
            lasers.cheat.choices = { { "Normal", "Normal" }, { "Massive", "Massive" }, { "Weak", "Weak" } };
            lasers.codes = { { "80071AB4", "0043" }, { "80071AB4", "0040" } };
            e.push_back(lasers);
        }
        {
            Entry size = gs("gs_tank_size", "Tank Size", "", {});
            size.cheat.description = "All tanks, from the next one made (a new level or a respawn).<br/><br/>GameShark code: <b>800718F5 00FF</b> (enlarged), <b>800718F5 0001</b> (shrunk) "
                "or <b>810718F4 3E20</b> (microscopic)";
            size.cheat.choices = { { "Normal", "Normal" }, { "Enlarged", "Enlarged" }, { "Shrunk", "Shrunk" }, { "Microscopic", "Microscopic" } };
            size.codes = { { "800718F5", "00FF" }, { "800718F5", "0001" }, { "810718F4", "3E20" } };
            e.push_back(size);
        }
        e.push_back(gs_hook("gs_enemies_frozen", "Enemies Can't Move", "", "80082854 0001", EnemiesCantMove));
        e.push_back(gs_hook("gs_turntable", "Enemies On Turntable", "", "8009D7AE 0020", EnemiesTurntable));
        e.push_back(pickup("gs_pickup_star", "Star Pickups", "81071D56", PickupStar));
        e.push_back(pickup("gs_pickup_ammo", "Ammo Pickups", "81071D62", PickupAmmo));
        e.push_back(pickup("gs_pickup_health", "Health Pickups", "81071D52", PickupHealth));
        e.push_back(pickup("gs_pickup_grenades", "Grenade Pickups", "81071D7A", PickupGrenades));
        e.push_back(pickup("gs_pickup_mines", "Mine Pickups", "81071D4E", PickupMines));
        e.push_back(pickup("gs_pickup_flamethrower", "Flamethrower Pickups", "81071D72", PickupFlamethrower));
        e.push_back(pickup("gs_pickup_radar", "Radar Pickups", "81071D82", PickupRadar));

        // Action Replay.
        e.push_back(ar_hook("ar_kill_counter", "Enemies Killed Counter Doesn't Increase", "", { "810A89F8", "2400" }, KillCounterFrozen));
        e.push_back(ar_hook("ar_guided_forever", "Guided Missiles Fly Forever", "", { "810DB8C4", "2400" }, GuidedForever));
        e.push_back(ar_hook("ar_gun_buddies", "Gun Buddies Are Invulnerable",
            "Nukes, powered up grenades and strong projectiles can still destroy them.", { "810E459C", "2400" }, GunBuddiesInvuln));
        e.push_back(ar_hook("ar_tank_ammo", "Tank Bullets Infinite Ammo", "", { "8108E97C", "2400" }, TankBulletsInfinite));
        e.push_back(ar_hook("ar_tank_bucks", "Infinite Tank Bucks", "", { "810D7420", "2400" }, TankBucksInfinite));
        e.push_back(ar_hook("ar_laser_shots", "Unlimited Laser Shots", "And powered up guided missiles.", { "810DC14C", "2400" }, UnlimitedLaser));
        e.push_back(ar_hook("ar_max_time", "Max Time Limit For Tank Wars", "", { "8109A9C6", "7FFF" }, MaxTimeTankWars));
        e.push_back(ar_hook("ar_special_ammo", "Infinite Ammo For Special Weapons", "",
            std::vector<const char*>(std::begin(special_ammo_codes), std::end(special_ammo_codes)), SpecialAmmoInfinite));
        {
            Entry menu = gs("ar_instant_menus", "Instant Menu Transitions", "", { "D0117EA3", "000F", "80117EA3", "00FF" }, false);
            menu.cheat.description = code_text("Action Replay", menu.codes[0]);
            menu.cheat.section = Section::ActionReplay;
            e.push_back(menu);
        }
        e.push_back(player_weapons(1, "811AD7EF", "811AD322", "811AD7EE"));
        e.push_back(player_weapons(2, "811AE187", "811ADCBA", "811AE186"));
        e.push_back(player_weapons(3, "811AEB1F", "811AE652", "811AEB1E"));
        e.push_back(player_weapons(4, "811AF4B7", "811AEFEA", "811AF4B6"));
        {
            Entry boss = gs("ar_annihilator", "Rename Tank Boss A To Annihilator", "",
                { "81075008", "414E", "8107500A", "4E49", "8107500C", "4849", "8107500E", "4C41", "81075010", "544F", "80075012", "0052" });
            boss.cheat.description = code_text("Action Replay", boss.codes[0]);
            boss.cheat.section = Section::ActionReplay;
            e.push_back(boss);
        }
        return e;
    }

    const std::vector<Entry>& entries() {
        static const std::vector<Entry> list = make_entries();
        return list;
    }

    // Set by the menu, read by the game thread.
    std::vector<std::atomic<uint32_t>>& wanted() {
        static std::vector<std::atomic<uint32_t>> values(entries().size());
        return values;
    }

    // Hook number -> entry.
    std::vector<int>& hook_entries() {
        static std::vector<int> map = [] {
            std::vector<int> ret(HookCount, -1);
            for (size_t i = 0; i < entries().size(); i++) {
                if (entries()[i].hook != NoHook) {
                    ret[entries()[i].hook] = (int)i;
                }
            }
            return ret;
        }();
        return map;
    }

    struct Code {
        uint8_t type;
        uint32_t addr;
        uint16_t value;
    };

    std::vector<Code> parse(const std::vector<const char*>& text) {
        std::vector<Code> ret;
        for (size_t i = 0; i + 1 < text.size(); i += 2) {
            uint32_t word = (uint32_t)std::strtoul(text[i], nullptr, 16);
            ret.push_back({ (uint8_t)(word >> 24), 0x80000000u | (word & 0xFFFFFF), (uint16_t)std::strtoul(text[i + 1], nullptr, 16) });
        }
        return ret;
    }

    // The game thread's own state for each entry.
    struct Applied {
        uint32_t value = 0;
        std::vector<std::vector<Code>> codes;
        std::unordered_map<uint32_t, uint8_t> saved; // original bytes, for restore
    };

    uint8_t read8(uint8_t* rdram, uint32_t addr) {
        return (uint8_t)MEM_B(0, (gpr)(int32_t)addr);
    }

    void write8(uint8_t* rdram, Applied& state, bool restore, uint32_t addr, uint8_t value) {
        if (addr >= 0x80800000u) {
            return;
        }
        if (restore) {
            state.saved.emplace(addr, read8(rdram, addr));
        }
        MEM_B(0, (gpr)(int32_t)addr) = (int8_t)value;
    }

    // 16-bit writes go in as two bytes, which also covers the odd addresses
    // in some of the codes.
    void write(uint8_t* rdram, Applied& state, bool restore, uint8_t type, uint32_t addr, uint16_t value) {
        if (type == 0x80) {
            write8(rdram, state, restore, addr, (uint8_t)value);
        }
        else if (type == 0x81) {
            write8(rdram, state, restore, addr, (uint8_t)(value >> 8));
            write8(rdram, state, restore, addr + 1, (uint8_t)value);
        }
    }

    uint16_t read16(uint8_t* rdram, uint32_t addr) {
        return (uint16_t)((read8(rdram, addr) << 8) | read8(rdram, addr + 1));
    }

    void run_codes(uint8_t* rdram, Applied& state, bool restore, const std::vector<Code>& codes) {
        for (size_t i = 0; i < codes.size(); i++) {
            const Code& c = codes[i];
            switch (c.type) {
                case 0x80:
                case 0x81:
                    write(rdram, state, restore, c.type, c.addr, c.value);
                    break;
                // Conditionals: the next line only runs if the test passes.
                case 0xD0: if (read8(rdram, c.addr) != (c.value & 0xFF)) i++; break;
                case 0xD1: if (read16(rdram, c.addr) != c.value) i++; break;
                case 0xD2: if (read8(rdram, c.addr) == (c.value & 0xFF)) i++; break;
                case 0xD3: if (read16(rdram, c.addr) == c.value) i++; break;
                // Repeat: 5000nnss vvvv runs the next line nn times, ss bytes
                // and vvvv apart.
                case 0x50:
                    if (i + 1 < codes.size()) {
                        const Code& next = codes[++i];
                        uint32_t count = (c.addr >> 8) & 0xFF;
                        uint32_t step = c.addr & 0xFF;
                        for (uint32_t k = 0; k < count; k++) {
                            write(rdram, state, restore, next.type, next.addr + k * step, (uint16_t)(next.value + k * c.value));
                        }
                    }
                    break;
            }
        }
    }

    void restore_bytes(uint8_t* rdram, Applied& state) {
        for (auto& [addr, value] : state.saved) {
            MEM_B(0, (gpr)(int32_t)addr) = (int8_t)value;
        }
        state.saved.clear();
    }

    void write_flag(uint8_t* rdram, const Entry& e, uint32_t value) {
        if (e.flag_size == 2) {
            MEM_H(0, (gpr)(int32_t)e.flag_addr) = (int16_t)value;
        }
        else {
            MEM_B(0, (gpr)(int32_t)e.flag_addr) = (int8_t)value;
        }
    }
}

const std::vector<Cheat>& btga::cheats::list() {
    static const std::vector<Cheat> list = [] {
        std::vector<Cheat> ret;
        for (const Entry& e : entries()) {
            ret.push_back(e.cheat);
        }
        return ret;
    }();
    return list;
}

void btga::cheats::set_value(size_t index, uint32_t value) {
    if (index < wanted().size()) {
        wanted()[index].store(value);
    }
}

// Each pass of the controller thread's loop (func_800988E8, 0x80098A08), on the
// menus and in game.
extern "C" void btga_cheats_frame(uint8_t* rdram) {
    static std::vector<Applied> applied = [] {
        std::vector<Applied> ret(entries().size());
        for (size_t i = 0; i < entries().size(); i++) {
            for (const auto& codes : entries()[i].codes) {
                ret[i].codes.push_back(parse(codes));
            }
        }
        return ret;
    }();

    for (size_t i = 0; i < entries().size(); i++) {
        const Entry& e = entries()[i];
        Applied& state = applied[i];
        uint32_t value = wanted()[i].load();
        if (value != state.value) {
            restore_bytes(rdram, state);
            if (e.flag_size != 0 && value == 0) {
                write_flag(rdram, e, 0);
            }
            state.value = value;
        }
        if (value == 0) {
            continue;
        }
        if (e.flag_size != 0) {
            write_flag(rdram, e, 1);
        }
        if (value - 1 < state.codes.size()) {
            run_codes(rdram, state, e.restore, state.codes[value - 1]);
        }
    }
}

// The hooks' question: is this cheat on (or which choice)?
extern "C" int btga_cheat(int hook) {
    if (hook < 0 || hook >= HookCount || hook_entries()[hook] < 0) {
        return 0;
    }
    return (int)wanted()[hook_entries()[hook]].load();
}

// Before the pickup jump table in func_8008ECB4 (0x8008ECF0): the case to run
// for case `index` (the item type minus one).
extern "C" int btga_cheat_pickup(int index) {
    static const std::pair<int, Hook> slots[] = {
        { 5, PickupMines }, { 6, PickupHealth }, { 7, PickupStar }, { 10, PickupAmmo },
        { 14, PickupFlamethrower }, { 16, PickupGrenades }, { 18, PickupRadar },
    };
    for (const auto& [slot, hook] : slots) {
        if (slot == index) {
            int choice = btga_cheat(hook);
            if (choice > 0 && choice <= (int)std::size(pickup_items)) {
                return pickup_items[choice - 1].index;
            }
        }
    }
    return index;
}
