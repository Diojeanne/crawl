/*
 *  File:       initfile.cc
 *  Summary:    Simple reading of an init file and system variables
 *  Written by: David Loewenstern
 *
 *  Modified for Crawl Reference by $Author$ on $Date$
 *
 *  Change History (most recent first):
 *
 *      <3>     5 May 2000      GDL             Add field stripping for 'name'
 *      <2>     6/12/99         BWR             Added get_system_environment
 *      <1>     6/9/99          DML             Created
 */

#include "AppHdr.h"
#include "externs.h"

#include "initfile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <ctype.h>

#include "chardump.h"
#include "clua.h"
#include "delay.h"
#include "directn.h"
#include "Kills.h"
#include "files.h"
#include "defines.h"
#ifdef USE_TILE
 #include "guic.h"
#endif
#include "invent.h"
#include "item_use.h"
#include "itemprop.h"
#include "libutil.h"
#include "message.h"
#include "mon-util.h"
#include "newgame.h"
#include "player.h"
#include "religion.h"
#include "stash.h"
#include "state.h"
#include "stuff.h"
#include "travel.h"
#include "items.h"
#include "view.h"

const std::string game_options::interrupt_prefix = "interrupt_";
game_options Options;

const static char *obj_syms = ")([/%.?=!.+\\0}X$";
const static int   obj_syms_len = 16;

template<class A, class B> void append_vector(A &dest, const B &src)
{
    dest.insert( dest.end(), src.begin(), src.end() );
}

god_type str_to_god(std::string god)
{
    if (god.empty())
        return (GOD_NO_GOD);

    lowercase(god);

    if (god == "random")
        return (GOD_RANDOM);

    for (int i = GOD_NO_GOD; i < NUM_GODS; ++i)
        if (lowercase_string(god_name(static_cast<god_type>(i))) == god)
            return (static_cast<god_type>(i));

    return (GOD_NO_GOD);
}

#ifdef USE_TILE
const std::string tile_cols[24] =
{
    "black", "darkgrey", "grey", "lightgrey", "white",
    "blue", "lightblue", "darkblue",
    "green", "lightgreen", "darkgreen",
    "cyan", "lightcyan", "darkcyan",
    "red", "lightred", "darkred",
    "magenta", "lightmagenta", "darkmagenta",
    "yellow", "lightyellow", "darkyellow", "brown"
};

static unsigned int _str_to_tile_colour(std::string colour)
{
    if (colour.empty())
        return (0);

    lowercase(colour);

    if (colour == "darkgray")
        colour = "darkgrey";
    else if (colour == "gray")
        colour = "grey";
    else if (colour == "lightgray")
        colour = "lightgrey";

    for (unsigned int i = 0; i < 24; i++)
    {
         if (tile_cols[i] == colour)
             return (i);
    }
    return (0);
}
#endif

const std::string cols[16] =
{
    "black", "blue", "green", "cyan", "red", "magenta", "brown",
    "lightgrey", "darkgrey", "lightblue", "lightgreen", "lightcyan",
    "lightred", "lightmagenta", "yellow", "white"
};

const char* colour_to_str(unsigned char colour)
{
    if ( colour >= 16 )
        return "lightgrey";
    else
        return cols[colour].c_str();
}

// returns -1 if unmatched else returns 0-15
int str_to_colour( const std::string &str, int default_colour,
                   bool accept_number )
{
    int ret;

    static const std::string element_cols[] =
    {
        "fire", "ice", "earth", "electricity", "air", "poison",
        "water", "magic", "mutagenic", "warp", "enchant", "heal",
        "holy", "dark", "death", "necro", "unholy", "vehumet",
        "beogh", "crystal", "blood", "smoke", "slime", "jewel",
        "elven", "dwarven", "orcish", "gila", "floor", "rock",
        "stone", "mist", "shimmer_blue", "decay", "silver", "gold",
        "iron", "bone", "random"
    };

    ASSERT(ARRAYSZ(element_cols) == (EC_RANDOM - EC_FIRE) + 1);

    for (ret = 0; ret < 16; ret++)
    {
        if (str == cols[ret])
            break;
    }

    // check for alternate spellings
    if (ret == 16)
    {
        if (str == "lightgray")
            ret = 7;
        else if (str == "darkgray")
            ret = 8;
    }

    if (ret == 16)
    {
        // Maybe we have an element colour attribute.
        for (unsigned i = 0; i < sizeof(element_cols) / sizeof(*element_cols);
                ++i)
        {
            if (str == element_cols[i])
            {
                // Ugh.
                ret = element_type(EC_FIRE + i);
                break;
            }
        }
    }

    if (ret == 16 && accept_number)
    {
        // Check if we have a direct colour index
        const char *s = str.c_str();
        char *es = NULL;
        const int ci = static_cast<int>(strtol(s, &es, 10));
        if (s != (const char *) es && es && ci >= 0 && ci < 16)
            ret = ci;
    }

    return ((ret == 16) ? default_colour : ret);
}

// returns -1 if unmatched else returns 0-15
static int _str_to_channel_colour( const std::string &str )
{
    int ret = str_to_colour( str );

    if (ret == -1)
    {
        if (str == "mute")
            ret = MSGCOL_MUTED;
        else if (str == "plain" || str == "off")
            ret = MSGCOL_PLAIN;
        else if (str == "default" || str == "on")
            ret = MSGCOL_DEFAULT;
        else if (str == "alternate")
            ret = MSGCOL_ALTERNATE;
    }

    return (ret);
}

static const std::string message_channel_names[ NUM_MESSAGE_CHANNELS ] =
{
    "plain", "prompt", "god", "pray", "duration", "danger", "warning", "food",
    "recovery", "sound", "talk", "talk_visual", "intrinsic_gain", "mutation",
    "monster_spell", "monster_enchant", "friend_spell", "friend_enchant",
    "monster_damage", "monster_target", "rotten_meat", "equipment", "floor",
    "multiturn", "examine", "examine_filter", "diagnostic","tutorial"
};

// returns -1 if unmatched else returns 0--(NUM_MESSAGE_CHANNELS-1)
int str_to_channel( const std::string &str )
{
    int ret;

    for (ret = 0; ret < NUM_MESSAGE_CHANNELS; ret++)
    {
        if (str == message_channel_names[ret])
            break;
    }

    return (ret == NUM_MESSAGE_CHANNELS ? -1 : ret);
}

std::string channel_to_str( int channel )
{
    if (channel < 0 || channel >= NUM_MESSAGE_CHANNELS)
        return "";

    return message_channel_names[channel];
}

static int _str_to_book( const std::string& str )
{
    if ( str == "fire" || str == "flame" )
        return SBT_FIRE;
    if ( str == "cold" || str == "ice" )
        return SBT_COLD;
    if ( str == "summ" || str == "summoning" )
        return SBT_SUMM;
    if ( str == "random" )
        return SBT_RANDOM;
    return SBT_NO_SELECTION;
}

static int _str_to_weapon( const std::string &str )
{
    if (str == "shortsword" || str == "short sword")
        return (WPN_SHORT_SWORD);
    else if (str == "mace")
        return (WPN_MACE);
    else if (str == "spear")
        return (WPN_SPEAR);
    else if (str == "trident")
        return (WPN_TRIDENT);
    else if (str == "hand axe" || str == "handaxe")
        return (WPN_HAND_AXE);
    else if (str == "random")
        return (WPN_RANDOM);

    return (WPN_UNKNOWN);
}

std::string weapon_to_str( int weapon )
{
    switch (weapon)
    {
    case WPN_SHORT_SWORD:
        return "short sword";
    case WPN_MACE:
        return "mace";
    case WPN_SPEAR:
        return "spear";
    case WPN_TRIDENT:
        return "trident";
    case WPN_HAND_AXE:
        return "hand axe";
    case WPN_RANDOM:
        return "random";
    default:
        return "random";
    }
}

static fire_type _str_to_fire_types( const std::string &str )
{
    if (str == "launcher")
        return (FIRE_LAUNCHER);
    else if (str == "dart")
        return (FIRE_DART);
    else if (str == "stone")
        return (FIRE_STONE);
    else if (str == "rock")
        return (FIRE_ROCK);
    else if (str == "dagger")
        return (FIRE_DAGGER);
    else if (str == "spear")
        return (FIRE_SPEAR);
    else if (str == "hand axe" || str == "handaxe" || str == "axe")
        return (FIRE_HAND_AXE);
    else if (str == "club")
        return (FIRE_CLUB);
    else if (str == "javelin")
        return (FIRE_JAVELIN);
    else if (str == "net")
        return (FIRE_NET);
    else if (str == "return" || str == "returning")
        return (FIRE_RETURNING);
    else if (str == "inscribed")
        return (FIRE_INSCRIBED);

    return (FIRE_NONE);
}

static char _str_to_race( const std::string &str )
{
    if (str == "random")
        return '*';

    int index = -1;

    if (str.length() == 1)      // old system of using menu letter
        return (str[0]);
    else if (str.length() == 2) // scan abbreviations
        index = get_species_index_by_abbrev( str.c_str() );

    // if we don't have a match, scan the full names
    if (index == -1)
        index = get_species_index_by_name( str.c_str() );

    if (index == -1)
        fprintf( stderr, "Unknown race choice: %s\n", str.c_str() );

    return ((index != -1) ? index_to_letter( index ) : 0);
}

static int _str_to_class( const std::string &str )
{
    if (str == "random")
        return '*';

    int index = -1;

    if (str.length() == 1)      // old system of using menu letter
        return (str[0]);
    else if (str.length() == 2) // scan abbreviations
        index = get_class_index_by_abbrev( str.c_str() );

    // if we don't have a match, scan the full names
    if (index == -1)
        index = get_class_index_by_name( str.c_str() );

    if (index == -1)
        fprintf( stderr, "Unknown class choice: %s\n", str.c_str() );

    return ((index != -1) ? index_to_letter( index ) : 0);
}

static bool _read_bool( const std::string &field, bool def_value )
{
    bool ret = def_value;

    if (field == "true" || field == "1" || field == "yes")
        ret = true;

    if (field == "false" || field == "0" || field == "no")
        ret = false;

    return (ret);
}

// read a value which can be either a boolean (in which case return
// 0 for true, -1 for false), or a string of the form PREFIX:NUMBER
// (e.g., auto:7), in which case return NUMBER as an int.
static int _read_bool_or_number( const std::string &field, int def_value,
                                 const std::string& num_prefix)
{
    int ret = def_value;

    if (field == "true" || field == "1" || field == "yes")
        ret = 0;

    if (field == "false" || field == "0" || field == "no")
        ret = -1;

    if ( field.find(num_prefix) == 0 )
        ret = atoi(field.c_str() + num_prefix.size());

    return (ret);
}


static unsigned curses_attribute(const std::string &field)
{
    if (field == "standout")               // probably reverses
        return CHATTR_STANDOUT;
    else if (field == "bold")              // probably brightens fg
        return CHATTR_BOLD;
    else if (field == "blink")             // probably brightens bg
        return CHATTR_BLINK;
    else if (field == "underline")
        return CHATTR_UNDERLINE;
    else if (field == "reverse")
        return CHATTR_REVERSE;
    else if (field == "dim")
        return CHATTR_DIM;
    else if (field.find("hi:") == 0 || field.find("hilite:") == 0 ||
             field.find("highlight:") == 0)
    {
        int col = field.find(":");
        int colour = str_to_colour(field.substr(col + 1));
        if (colour == -1)
            crawl_state.add_startup_error(
                make_stringf("Bad highlight string -- %s\n", field.c_str()));
        else
            return CHATTR_HILITE | (colour << 8);
    }
    else if (field != "none")
        crawl_state.add_startup_error(
            make_stringf( "Bad colour -- %s\n", field.c_str() ) );
    return CHATTR_NORMAL;
}

void game_options::new_dump_fields(const std::string &text, bool add)
{
    // Easy; chardump.cc has most of the intelligence.
    std::vector<std::string> fields = split_string(",", text, true, true);
    if (add)
        append_vector(dump_order, fields);
    else
    {
        for (int f = 0, size = fields.size(); f < size; ++f)
            for (int i = 0, dsize = dump_order.size(); i < dsize; ++i)
            {
                if (dump_order[i] == fields[f])
                {
                    dump_order.erase( dump_order.begin() + i );
                    break;
                }
            }
    }
}

void game_options::reset_startup_options()
{
    race                   = 0;
    cls                    = 0;
    weapon                 = WPN_UNKNOWN;
    book                   = SBT_NO_SELECTION;
    random_pick            = false;
    chaos_knight           = GOD_NO_GOD;
    death_knight           = DK_NO_SELECTION;
    priest                 = GOD_NO_GOD;
}

void game_options::set_default_activity_interrupts()
{
    for (int adelay = 0; adelay < NUM_DELAYS; ++adelay)
        for (int aint = 0; aint < NUM_AINTERRUPTS; ++aint)
            activity_interrupts[adelay][aint] = true;

    const char *default_activity_interrupts[] = {
        "interrupt_armour_on = hp_loss, monster_attack",
        "interrupt_armour_off = interrupt_armour_on",
        "interrupt_drop_item = interrupt_armour_on",
        "interrupt_jewellery_on = interrupt_armour_on",
        "interrupt_memorise = interrupt_armour_on, stat",
        "interrupt_butcher = interrupt_armour_on, teleport, stat",
        "interrupt_bottle_blood = interrupt_butcher",
        "interrupt_offer_corpse = interrupt_butcher, hungry",
        "interrupt_vampire_feed = interrupt_butcher",
        "interrupt_passwall = interrupt_butcher",
        "interrupt_multidrop = interrupt_butcher",
        "interrupt_macro = interrupt_multidrop",
        "interrupt_travel = interrupt_butcher, statue, hungry, "
                            "burden, monster, hit_monster",
        "interrupt_run = interrupt_travel, message",
        "interrupt_rest = interrupt_run",

        // Stair ascents/descents cannot be interrupted, attempts to interrupt
        // the delay will just trash all queued delays, including travel.
        "interrupt_ascending_stairs =",
        "interrupt_descending_stairs =",
        "interrupt_recite = teleport",
        "interrupt_uninterruptible =",
        "interrupt_weapon_swap =",

        NULL
    };

    for (int i = 0; default_activity_interrupts[i]; ++i)
        read_option_line( default_activity_interrupts[i], false );
}

void game_options::clear_activity_interrupts(
        FixedVector<bool, NUM_AINTERRUPTS> &eints)
{
    for (int i = 0; i < NUM_AINTERRUPTS; ++i)
        eints[i] = false;
}

void game_options::set_activity_interrupt(
        FixedVector<bool, NUM_AINTERRUPTS> &eints,
        const std::string &interrupt)
{
    if (interrupt.find(interrupt_prefix) == 0)
    {
        std::string delay_name = interrupt.substr( interrupt_prefix.length() );
        delay_type delay = get_delay(delay_name);
        if (delay == NUM_DELAYS)
        {
            crawl_state.add_startup_error(
                make_stringf("Unknown delay: %s\n", delay_name.c_str()));
            return;
        }

        FixedVector<bool, NUM_AINTERRUPTS> &refints =
            activity_interrupts[delay];

        for (int i = 0; i < NUM_AINTERRUPTS; ++i)
            if (refints[i])
                eints[i] = true;

        return;
    }

    activity_interrupt_type ai = get_activity_interrupt(interrupt);
    if (ai == NUM_AINTERRUPTS)
    {
        crawl_state.add_startup_error(
            make_stringf("Delay interrupt name \"%s\" not recognised.\n",
                         interrupt.c_str()));
        return;
    }

    eints[ai] = true;
}

void game_options::set_activity_interrupt(const std::string &activity_name,
                                          const std::string &interrupt_names,
                                          bool append_interrupts,
                                          bool remove_interrupts)
{
    const delay_type delay = get_delay(activity_name);
    if (delay == NUM_DELAYS)
    {
        crawl_state.add_startup_error(
            make_stringf("Unknown delay: %s\n", activity_name.c_str()));
        return;
    }

    std::vector<std::string> interrupts = split_string(",", interrupt_names);
    FixedVector<bool, NUM_AINTERRUPTS> &eints = activity_interrupts[ delay ];

    if (remove_interrupts)
    {
        FixedVector<bool, NUM_AINTERRUPTS> refints;
        clear_activity_interrupts(refints);
        for (int i = 0, size = interrupts.size(); i < size; ++i)
            set_activity_interrupt(refints, interrupts[i]);

        for (int i = 0; i < NUM_AINTERRUPTS; ++i)
            if (refints[i])
                eints[i] = false;
    }
    else
    {
        if (!append_interrupts)
            clear_activity_interrupts(eints);

        for (int i = 0, size = interrupts.size(); i < size; ++i)
            set_activity_interrupt(eints, interrupts[i]);
    }

    eints[AI_FORCE_INTERRUPT] = true;
}

void game_options::reset_options()
{
    set_default_activity_interrupts();

    reset_startup_options();

#if defined(SAVE_DIR_PATH)
    save_dir  = SAVE_DIR_PATH;
#elif !defined(DOS)
    save_dir = "saves/";
#else
    save_dir.clear();
#endif

#if !defined(SHORT_FILE_NAMES) && !defined(SAVE_DIR_PATH)
    morgue_dir = "morgue/";
#endif
    additional_macro_files.clear();

    player_name.clear();

#ifdef DGL_SIMPLE_MESSAGING
    messaging = true;
#endif

    mouse_input = false;

    view_max_width   = 33;
    view_max_height  = 21;
    mlist_min_height = 5;
    msg_max_height   = 10;
    mlist_allow_alternate_layout = false;
    classic_hud = false;

    view_lock_x = true;
    view_lock_y = true;

    center_on_scroll = false;
    symmetric_scroll = true;
    scroll_margin_x  = 2;
    scroll_margin_y  = 2;

    autopickup_on = true;
    autoprayer_on = false;
    default_friendly_pickup = FRIENDLY_PICKUP_FRIEND;
    show_more_prompt = true;

    show_gold_turns = false;
    show_beam       = false;

    use_old_selection_order = false;
    prev_race   = 0;
    prev_cls    = 0;
    prev_ck     = GOD_NO_GOD;
    prev_dk     = DK_NO_SELECTION;
    prev_pr     = GOD_NO_GOD;
    prev_weapon = WPN_UNKNOWN;
    prev_book   = SBT_NO_SELECTION;
    prev_randpick = false;
    remember_name = true;

#ifdef USE_ASCII_CHARACTERS
    char_set               = CSET_ASCII;
#else
    char_set               = CSET_IBM;
#endif

    // set it to the .crawlrc default
    autopickups = ((1L << 15) | // gold
                   (1L <<  6) | // scrolls
                   (1L <<  8) | // potions
                   (1L << 10) | // books
                   (1L <<  7) | // jewellery
                   (1L <<  3) | // wands
                   (1L <<  4)); // food

    suppress_startup_errors = false;

    show_inventory_weights = false;
    colour_map             = true;
    clean_map              = false;
    show_uncursed          = true;
    easy_open              = true;
    easy_unequip           = true;
    easy_butcher           = true;
    always_confirm_butcher = false;
    list_rotten            = true;
    easy_confirm           = CONFIRM_SAFE_EASY;
    easy_quit_item_prompts = true;
    hp_warning             = 10;
    magic_point_warning    = 0;
    default_target         = true;
    autopickup_no_burden   = false;

    user_note_prefix       = "";
    note_all_skill_levels  = false;
    note_skill_max         = false;
    note_all_spells        = false;
    note_hp_percent        = 5;
    ood_interesting        = 8;

    // [ds] Grumble grumble.
    auto_list              = true;

    delay_message_clear    = false;
    pickup_dropped         = false;
    pickup_thrown          = true;

    travel_delay           = 20;
    travel_stair_cost      = 500;

    // Sort only pickup menus by default.
    sort_menus.clear();
    set_menu_sort("pickup: true");

    tc_reachable           = BLUE;
    tc_excluded            = LIGHTMAGENTA;
    tc_exclude_circle      = RED;
    tc_dangerous           = CYAN;
    tc_disconnected        = DARKGREY;

    show_waypoints         = true;
    item_colour            = true;

    // [ds] Default to jazzy colours.
    detected_item_colour   = GREEN;
    detected_monster_colour= LIGHTRED;
    status_caption_colour  = BROWN;

#ifdef USE_TILE
    classic_item_colours   = true;
#else
    classic_item_colours   = false;
#endif

    easy_exit_menu         = true;
#ifdef DOS
    dos_use_background_intensity = false;
#else
    dos_use_background_intensity = true;
#endif

    level_map_title        = true;

    assign_item_slot       = SS_FORWARD;

    macro_meta_entry       = true;

    // 10 was the cursor step default on Linux.
    level_map_cursor_step  = 7;
    use_fake_cursor        = false;

    stash_tracking         = STM_ALL;

    explore_stop           = ES_ITEM | ES_STAIR | ES_PORTAL | ES_SHOP | ES_ALTAR
                                     | ES_GREEDY_PICKUP;

    // The prompt conditions will be combined into explore_stop after
    // reading options.
    explore_stop_prompt    = ES_NONE;

    explore_item_greed     = 10;
    explore_greedy         = true;

    explore_improved       = false;
    trap_prompt            = true;

    target_zero_exp        = false;
    target_wrap            = true;
    target_oos             = true;
    target_los_first       = true;
    target_unshifted_dirs  = false;

    dump_kill_places       = KDO_ONE_PLACE;
    dump_message_count     = 7;
    dump_item_origins      = IODS_ARTEFACTS | IODS_RODS;
    dump_item_origin_price = -1;

    drop_mode              = DM_MULTI;
    pickup_mode            = -1;

    flush_input[ FLUSH_ON_FAILURE ]     = true;
    flush_input[ FLUSH_BEFORE_COMMAND ] = false;
    flush_input[ FLUSH_ON_MESSAGE ]     = false;
    flush_input[ FLUSH_LUA ]            = true;

    fire_items_start       = 0;           // start at slot 'a'

    // Clear fire_order and set up the defaults.
    set_fire_order("launcher, return, "
                   "javelin / dart / stone / rock /"
                   " spear / net / handaxe / dagger / club, inscribed",
                   false);

    item_stack_summary_minimum = 5;

#ifdef WIZARD
    fsim_rounds = 40000L;
    fsim_mons   = "worm";
    fsim_str = fsim_int = fsim_dex = 15;
    fsim_xl  = 10;
    fsim_kit.clear();
#endif

    // These are only used internally, and only from the commandline:
    // XXX: These need a better place.
    sc_entries             = 0;
    sc_format              = -1;

    friend_brand       = CHATTR_NORMAL;
    neutral_brand      = CHATTR_NORMAL;
    stab_brand         = CHATTR_NORMAL;
    may_stab_brand     = CHATTR_NORMAL;
    heap_brand         = CHATTR_REVERSE;
    feature_item_brand = CHATTR_REVERSE;
    trap_item_brand    = CHATTR_NORMAL;

    no_dark_brand    = true;

#ifdef WIZARD
    wiz_mode         = WIZ_NO;
#endif

#ifdef USE_TILE
    tile_show_items[0]   = '0';
    tile_title_screen    = true;
    // minimap colours
    tile_player_col      = MAP_WHITE;
    tile_monster_col     = MAP_RED;
    tile_neutral_col     = MAP_RED;
    tile_friendly_col    = MAP_LTRED;
    tile_plant_col       = MAP_DKGREEN;
    tile_item_col        = MAP_GREEN;
    tile_unseen_col      = MAP_BLACK;
    tile_floor_col       = MAP_LTGREY;
    tile_wall_col        = MAP_DKGREY;
    tile_mapped_wall_col = MAP_BLUE;
    tile_door_col        = MAP_BROWN;
    tile_downstairs_col  = MAP_MAGENTA;
    tile_upstairs_col    = MAP_BLUE;
    tile_feature_col     = MAP_CYAN;
    tile_trap_col        = MAP_YELLOW;
    tile_water_col       = MAP_MDGREY;
    tile_lava_col        = MAP_MDGREY;
    tile_excluded_col    = MAP_DKCYAN;
    tile_excl_centre_col = MAP_DKBLUE;
#endif

#ifdef WIN32TILES
    use_dos_char = true;
#endif

    // map each colour to itself as default
#ifdef USE_8_COLOUR_TERM_MAP
    for (int i = 0; i < 16; i++)
        colour[i] = i % 8;

    colour[ DARKGREY ] = COL_TO_REPLACE_DARKGREY;
#else
    for (int i = 0; i < 16; i++)
        colour[i] = i;
#endif

    // map each channel to plain (well, default for now since I'm testing)
    for (int i = 0; i < NUM_MESSAGE_CHANNELS; i++)
        channels[i] = MSGCOL_DEFAULT;

    // Clear vector options.
    dump_order.clear();
    new_dump_fields("header,hiscore,stats,misc,inventory,"
                    "skills,spells,overview,mutations,messages,"
                    "screenshot,monlist,kills,notes");

    hp_colour.clear();
    hp_colour.push_back(std::pair<int,int>(50, YELLOW));
    hp_colour.push_back(std::pair<int,int>(25, RED));
    mp_colour.clear();
    mp_colour.push_back(std::pair<int, int>(50, YELLOW));
    mp_colour.push_back(std::pair<int, int>(25, RED));

    never_pickup.clear();
    always_pickup.clear();
    note_monsters.clear();
    note_messages.clear();
    autoinscriptions.clear();
    note_items.clear();
    note_skill_levels.clear();
    travel_stop_message.clear();
    sound_mappings.clear();
    menu_colour_mappings.clear();
    menu_colour_prefix_class = false;
    menu_colour_prefix_id    = false;
    message_colour_mappings.clear();
    drop_filter.clear();
    map_file_name.clear();
    named_options.clear();

    clear_cset_overrides();
    clear_feature_overrides();
    mon_glyph_overrides.clear();

    // Map each category to itself. The user can override in init.txt
    kill_map[KC_YOU] = KC_YOU;
    kill_map[KC_FRIENDLY] = KC_FRIENDLY;
    kill_map[KC_OTHER] = KC_OTHER;

    // Setup travel information. What's a better place to do this?
    initialise_travel();
}

void game_options::clear_cset_overrides()
{
    memset(cset_override, 0, sizeof cset_override);
}

void game_options::clear_feature_overrides()
{
    feature_overrides.clear();
}

static unsigned read_symbol(std::string s)
{
    if (s.empty())
        return (0);

    if (s.length() > 1 && s[0] == '\\')
        s = s.substr(1);

    if (s.length() == 1)
        return s[0];

    int base = 10;
    if (s.length() > 1 && s[0] == 'x')
    {
        s = s.substr(1);
        base = 16;
    }

    char *tail;
    return (strtoul(s.c_str(), &tail, base));
}

void game_options::set_fire_order(const std::string &s, bool add)
{
    if (!add)
        fire_order.clear();
    std::vector<std::string> slots = split_string(",", s);
    for (int i = 0, size = slots.size(); i < size; ++i)
        add_fire_order_slot(slots[i]);
}

void game_options::add_fire_order_slot(const std::string &s)
{
    unsigned flags = 0;
    std::vector<std::string> alts = split_string("/", s);
    for (int i = 0, size = alts.size(); i < size; ++i)
        flags |= _str_to_fire_types(alts[i]);

    if (flags)
        fire_order.push_back(flags);
}

void game_options::add_mon_glyph_override(monster_type mtype,
                                          mon_display &mdisp)
{
    mdisp.type = mtype;
    mon_glyph_overrides.push_back(mdisp);
}

void game_options::add_mon_glyph_overrides(const std::string &mons,
                                           mon_display &mdisp)
{
    // If one character, this is a monster letter.
    int letter = -1;
    if (mons.length() == 1)
        letter = mons[0] == '_' ? ' ' : mons[0];

    bool found = false;
    for (int i = 0; i < NUM_MONSTERS; ++i)
    {
        const monsterentry *me = get_monster_data(i);
        if (!me || me->mc == MONS_PROGRAM_BUG)
            continue;

        if (me->showchar == letter || me->name == mons)
        {
            found = true;
            add_mon_glyph_override(static_cast<monster_type>(i), mdisp);
        }
    }
    if (!found)
        crawl_state.add_startup_error(
            make_stringf("Unknown monster: \"%s\"", mons.c_str()));
}

mon_display game_options::parse_mon_glyph(const std::string &s) const
{
    mon_display md;
    std::vector<std::string> phrases = split_string(" ", s);
    for (int i = 0, size = phrases.size(); i < size; ++i)
    {
        const std::string &p = phrases[i];
        const int col = str_to_colour(p, -1, false);
        if (col != -1 && colour)
            md.colour = col;
        else
            md.glyph = p == "_"? ' ' : read_symbol(p);
    }
    return (md);
}

void game_options::add_mon_glyph_override(const std::string &text)
{
    std::vector<std::string> override = split_string(":", text);
    if (override.size() != 2u)
        return;

    mon_display mdisp = parse_mon_glyph(override[1]);
    if (mdisp.glyph || mdisp.colour)
        add_mon_glyph_overrides(override[0], mdisp);
}

void game_options::add_feature_override(const std::string &text)
{
    std::string::size_type epos = text.rfind("}");
    if (epos == std::string::npos)
        return;

    std::string::size_type spos = text.rfind("{", epos);
    if (spos == std::string::npos)
        return;

    std::string fname = text.substr(0, spos);
    std::string props = text.substr(spos + 1, epos - spos - 1);
    std::vector<std::string> iprops = split_string(",", props, true, true);

    if (iprops.size() < 1 || iprops.size() > 7)
        return;

    if (iprops.size() < 7)
        iprops.resize(7);

    trim_string(fname);
    std::vector<dungeon_feature_type> feats =
        features_by_desc(text_pattern(fname));
    if (feats.empty())
        return;

    for (int i = 0, size = feats.size(); i < size; ++i)
    {
        feature_override fov;
        fov.feat = feats[i];

        fov.override.symbol         = read_symbol(iprops[0]);
        fov.override.magic_symbol   = read_symbol(iprops[1]);
        fov.override.colour         = str_to_colour(iprops[2], BLACK);
        fov.override.map_colour     = str_to_colour(iprops[3], BLACK);
        fov.override.seen_colour    = str_to_colour(iprops[4], BLACK);
        fov.override.em_colour      = str_to_colour(iprops[5], BLACK);
        fov.override.seen_em_colour = str_to_colour(iprops[6], BLACK);

        feature_overrides.push_back(fov);
    }
}

void game_options::add_cset_override(
        char_set_type set, const std::string &overrides)
{
    std::vector<std::string> overs = split_string(",", overrides);
    for (int i = 0, size = overs.size(); i < size; ++i)
    {
        std::vector<std::string> mapping = split_string(":", overs[i]);
        if (mapping.size() != 2)
            continue;

        dungeon_char_type dc = dchar_by_name(mapping[0]);
        if (dc == NUM_DCHAR_TYPES)
            continue;

        unsigned symbol =
            static_cast<unsigned>(read_symbol(mapping[1]));

        if (set == NUM_CSET)
            for (int c = 0; c < NUM_CSET; ++c)
                add_cset_override(char_set_type(c), dc, symbol);
        else
            add_cset_override(set, dc, symbol);
    }
}

void game_options::add_cset_override(char_set_type set, dungeon_char_type dc,
                                     unsigned symbol)
{
    cset_override[set][dc] = symbol;
}

// returns where the init file was read from
std::string read_init_file(bool runscript)
{
    const char* locations_data[][2] = {
        { SysEnv.crawl_rc.c_str(), "" },
        { SysEnv.crawl_dir.c_str(), "init.txt" },
#ifdef MULTIUSER
        { SysEnv.home.c_str(), "/.crawlrc" },
        { SysEnv.home.c_str(), "init.txt" },
#endif
        { "", "init.txt" },
#ifdef WIN32CONSOLE
        { "", ".crawlrc" },
        { "", "_crawlrc" },
#endif
        { NULL, NULL }                // placeholder to mark end
    };

    Options.reset_options();

    FILE* f = NULL;
    char name_buff[kPathLen];
    // Check all possibilities for init.txt
    for ( int i = 0; f == NULL && locations_data[i][1] != NULL; ++i )
    {
        // Don't look at unset options
        if ( locations_data[i][0] != NULL )
        {
            snprintf( name_buff, sizeof name_buff, "%s%s",
                      locations_data[i][0], locations_data[i][1] );
            f = fopen( name_buff, "r" );
        }
    }

    if ( f == NULL )
    {
#ifdef MULTIUSER
        return "not found (~/.crawlrc missing)";
#else
        return "not found (init.txt missing from current directory)";
#endif
    }

    read_options(f, runscript);
    fclose(f);
    return std::string(name_buff);
}                               // end read_init_file()

void read_startup_prefs()
{
#ifndef DISABLE_STICKY_STARTUP_OPTIONS
    std::string fn = get_prefs_filename();
    FILE *f = fopen(fn.c_str(), "r");
    if (!f)
        return;

    game_options temp;
    FileLineInput fl(f);
    temp.read_options(fl, false);
    fclose(f);

    Options.prev_randpick = temp.random_pick;
    Options.prev_weapon   = temp.weapon;
    Options.prev_pr       = temp.priest;
    Options.prev_dk       = temp.death_knight;
    Options.prev_ck       = temp.chaos_knight;
    Options.prev_cls      = temp.cls;
    Options.prev_race     = temp.race;
    Options.prev_book     = temp.book;
    Options.prev_name     = temp.player_name;
#endif // !DISABLE_STICKY_STARTUP_OPTIONS
}

#ifndef DISABLE_STICKY_STARTUP_OPTIONS
static void write_newgame_options(FILE *f)
{
    // Write current player name
    fprintf(f, "name = %s\n", you.your_name);

    if (Options.prev_randpick)
        Options.prev_race = Options.prev_cls = '*';

    // Race selection
    if (Options.prev_race)
        fprintf(f, "race = %c\n", Options.prev_race);
    if (Options.prev_cls)
        fprintf(f, "class = %c\n", Options.prev_cls);

    if (Options.prev_weapon != WPN_UNKNOWN)
        fprintf(f, "weapon = %s\n", weapon_to_str(Options.prev_weapon).c_str());

    if (Options.prev_ck != GOD_NO_GOD)
    {
        fprintf(f, "chaos_knight = %s\n",
                Options.prev_ck == GOD_XOM? "xom" :
                Options.prev_ck == GOD_MAKHLEB? "makhleb" :
                                            "random");
    }
    if (Options.prev_dk != DK_NO_SELECTION)
    {
        fprintf(f, "death_knight = %s\n",
                Options.prev_dk == DK_NECROMANCY? "necromancy" :
                Options.prev_dk == DK_YREDELEMNUL? "yredelemnul" :
                                            "random");
    }
    if (is_priest_god(Options.prev_pr) || Options.prev_pr == GOD_RANDOM)
    {
        fprintf(f, "priest = %s\n",
                lowercase_string(god_name(Options.prev_pr)).c_str());
    }

    if (Options.prev_book != SBT_NO_SELECTION )
    {
        fprintf(f, "book = %s\n",
                Options.prev_book == SBT_FIRE ? "fire" :
                Options.prev_book == SBT_COLD ? "cold" :
                Options.prev_book == SBT_SUMM ? "summ" :
                "random");
    }
}
#endif // !DISABLE_STICKY_STARTUP_OPTIONS

void write_newgame_options_file()
{
#ifndef DISABLE_STICKY_STARTUP_OPTIONS
    std::string fn = get_prefs_filename();
    FILE *f = fopen(fn.c_str(), "w");
    if (!f)
        return;
    write_newgame_options(f);
    fclose(f);
#endif // !DISABLE_STICKY_STARTUP_OPTIONS
}

void save_player_name()
{
#ifndef DISABLE_STICKY_STARTUP_OPTIONS
    if (!Options.remember_name)
        return ;

    // Read other preferences
    read_startup_prefs();

    // And save
    write_newgame_options_file();
#endif // !DISABLE_STICKY_STARTUP_OPTIONS
}

void read_options(FILE *f, bool runscript)
{
    FileLineInput fl(f);
    Options.read_options(fl, runscript);
}

void read_options(const std::string &s, bool runscript, bool clear_aliases)
{
    StringLineInput st(s);
    Options.read_options(st, runscript, clear_aliases);
}

game_options::game_options()
{
    reset_options();
}

void game_options::read_options(InitLineInput &il, bool runscript,
                                bool clear_aliases)
{
    unsigned int line = 0;

    bool inscriptblock = false;
    bool inscriptcond  = false;
    bool isconditional = false;

    bool l_init        = false;

    if (clear_aliases)
        aliases.clear();

    std::string luacond;
    std::string luacode;
    while (!il.eof())
    {
        std::string s   = il.getline();
        std::string str = s;
        line++;

        trim_string( str );

        // This is to make some efficient comments
        if ((str.empty() || str[0] == '#') && !inscriptcond && !inscriptblock)
            continue;

        if (!inscriptcond && str[0] == ':')
        {
            // The init file is now forced into isconditional mode.
            isconditional = true;
            str = str.substr(1);
            if (!str.empty() && runscript)
            {
                // If we're in the middle of an option block, close it.
                if (luacond.length() && l_init)
                {
                    luacond += "]] )\n";
                    l_init = false;
                }
                luacond += str + "\n";
            }
            continue;
        }
        if (!inscriptcond && (str.find("L<") == 0 || str.find("<") == 0))
        {
            // The init file is now forced into isconditional mode.
            isconditional = true;
            inscriptcond  = true;

            str = str.substr( str.find("L<") == 0? 2 : 1 );
            // Is this a one-liner?
            if (!str.empty() && str[ str.length() - 1 ] == '>')
            {
                inscriptcond = false;
                str = str.substr(0, str.length() - 1);
            }

            if (!str.empty() && runscript)
            {
                // If we're in the middle of an option block, close it.
                if (luacond.length() && l_init)
                {
                    luacond += "]] )\n";
                    l_init = false;
                }
                luacond += str + "\n";
            }
            continue;
        }
        else if (inscriptcond &&
                (str.find(">") == str.length() - 1 || str == ">L"))
        {
            inscriptcond = false;
            str = str.substr(0, str.length() - 1);
            if (!str.empty() && runscript)
                luacond += str + "\n";
            continue;
        }
        else if (inscriptcond)
        {
            if (runscript)
                luacond += s + "\n";
            continue;
        }

        // Handle blocks of Lua
        if (!inscriptblock && (str.find("Lua{") == 0 || str.find("{") == 0))
        {
            inscriptblock = true;
            luacode.clear();

            // Strip leading Lua[
            str = str.substr( str.find("Lua{") == 0? 4 : 1 );

            if (!str.empty() && str.find("}") == str.length() - 1)
            {
                str = str.substr(0, str.length() - 1);
                inscriptblock = false;
            }

            if (!str.empty())
                luacode += str + "\n";

            if (!inscriptblock && runscript)
            {
#ifdef CLUA_BINDINGS
                clua.execstring(luacode.c_str());
                if (!clua.error.empty())
                    mprf(MSGCH_WARN, "Lua error: %s\n", clua.error.c_str());
                luacode.clear();
#endif
            }

            continue;
        }
        else if (inscriptblock && (str == "}Lua" || str == "}"))
        {
            inscriptblock = false;
#ifdef CLUA_BINDINGS
            if (runscript)
            {
                clua.execstring(luacode.c_str());
                if (!clua.error.empty())
                    mprf(MSGCH_WARN, "Lua error: %s\n", clua.error.c_str());
            }
#endif
            luacode.clear();
            continue;
        }
        else if (inscriptblock)
        {
            luacode += s + "\n";
            continue;
        }

        if (isconditional && runscript)
        {
            if (!l_init)
            {
                luacond += "crawl.setopt( [[\n";
                l_init = true;
            }

            luacond += s + "\n";
            continue;
        }

        read_option_line(str, runscript);
    }

#ifdef CLUA_BINDINGS
    if (runscript && !luacond.empty())
    {
        if (l_init)
            luacond += "]] )\n";
        clua.execstring(luacond.c_str());
        if (!clua.error.empty())
            mprf(MSGCH_WARN, "Lua error: %s\n", clua.error.c_str());
    }
#endif

    Options.explore_stop |= Options.explore_stop_prompt;
}

void game_options::fixup_options()
{
    // Validate save_dir
    if (!check_dir("Save directory", save_dir))
        end(1);

    if (!SysEnv.morgue_dir.empty())
        morgue_dir = SysEnv.morgue_dir;

    if (!check_dir("Morgue directory", morgue_dir))
        end(1);
}

static int _str_to_killcategory(const std::string &s)
{
   static const char *kc[] = {
       "you",
       "friend",
       "other",
   };

   for (unsigned i = 0; i < sizeof(kc) / sizeof(*kc); ++i)
   {
       if (s == kc[i])
           return i;
   }
   return -1;
}

void game_options::do_kill_map(const std::string &from, const std::string &to)
{
    int ifrom = _str_to_killcategory(from),
        ito   = _str_to_killcategory(to);
    if (ifrom != -1 && ito != -1)
        kill_map[ifrom] = ito;
}

int game_options::read_explore_stop_conditions(const std::string &field) const
{
    int conditions = 0;
    std::vector<std::string> stops = split_string(",", field);
    for (int i = 0, count = stops.size(); i < count; ++i)
    {
        const std::string &c = stops[i];
        if (c == "item" || c == "items")
            conditions |= ES_ITEM;
        else if (c == "pickup")
            conditions |= ES_PICKUP;
        else if (c == "greedy_pickup" || c == "greedy pickup")
            conditions |= ES_GREEDY_PICKUP;
        else if (c == "shop" || c == "shops")
            conditions |= ES_SHOP;
        else if (c == "stair" || c == "stairs")
            conditions |= ES_STAIR;
        else if (c == "altar" || c == "altars")
            conditions |= ES_ALTAR;
        else if (c == "greedy_item" || c == "greedy_items")
            conditions |= ES_GREEDY_ITEM;
    }
    return (conditions);
}

void game_options::add_alias(const std::string &key, const std::string &val)
{
    aliases[key] = val;
}

std::string game_options::unalias(const std::string &key) const
{
    std::map<std::string, std::string>::const_iterator i = aliases.find(key);
    return (i == aliases.end()? key : i->second);
}

void game_options::add_message_colour_mappings(const std::string &field)
{
    std::vector<std::string> fragments = split_string(",", field);
    for (int i = 0, count = fragments.size(); i < count; ++i)
        add_message_colour_mapping(fragments[i]);
}

message_filter game_options::parse_message_filter(const std::string &filter)
{
    std::string::size_type pos = filter.find(":");
    if (pos && pos != std::string::npos)
    {
        std::string prefix = filter.substr(0, pos);
        int channel = str_to_channel( prefix );
        if (channel != -1 || prefix == "any")
        {
            std::string s = filter.substr( pos + 1 );
            trim_string( s );
            return message_filter( channel, s );
        }
    }

    return message_filter( filter );
}

void game_options::add_message_colour_mapping(const std::string &field)
{
    std::vector<std::string> cmap = split_string(":", field, true, true, 1);

    if (cmap.size() != 2)
        return;

    const int col = (cmap[0] == "mute") ? MSGCOL_MUTED
                                        : str_to_colour(cmap[0]);
    if (col == -1)
        return;

    message_colour_mapping m = { parse_message_filter( cmap[1] ), col };
    message_colour_mappings.push_back( m );
}

// Option syntax is:
// sort_menu = [menu_type:]yes|no|auto:n[:sort_conditions]
void game_options::set_menu_sort(std::string field)
{
    if (field.empty())
        return;

    menu_sort_condition cond(field);

    // Overrides all previous settings.
    if (cond.mtype == MT_ANY)
        sort_menus.clear();

    // Override existing values, if necessary.
    for (unsigned int i = 0; i < sort_menus.size(); i++)
        if (sort_menus[i].mtype == cond.mtype)
        {
            sort_menus[i].sort = cond.sort;
            sort_menus[i].cmp  = cond.cmp;
            return;
        }

    sort_menus.push_back(cond);
}

void game_options::split_parse(
    const std::string &s, const std::string &separator,
    void (game_options::*add)(const std::string &))
{
    const std::vector<std::string> defs = split_string(separator, s);
    for (int i = 0, size = defs.size(); i < size; ++i)
        (this->*add)( defs[i] );
}

void game_options::set_option_fragment(const std::string &s)
{
    if (s.empty())
        return;

    std::string::size_type st = s.find(':');
    if (st == std::string::npos)
    {
        // Boolean option.
        if (s[0] == '!')
            read_option_line(s.substr(1) + " = false");
        else
            read_option_line(s + " = true");
    }
    else
    {
        // key:val option.
        read_option_line(s.substr(0, st) + " = " + s.substr(st + 1));
    }
}

void game_options::read_option_line(const std::string &str, bool runscript)
{
#define BOOL_OPTION_NAMED(_opt_str, _opt_var)               \
    if (key == _opt_str) do {                               \
        this->_opt_var = _read_bool(field, this->_opt_var); \
    } while (false)
#define BOOL_OPTION(_opt) BOOL_OPTION_NAMED(#_opt, _opt)

#define COLOUR_OPTION_NAMED(_opt_str, _opt_var)                         \
    if (key == _opt_str) do {                                           \
        const int col = str_to_colour( field );                         \
        if (col != -1) {                                                \
            this->_opt_var = col;                                       \
        } else {                                                        \
            /*fprintf( stderr, "Bad %s -- %s\n", key, field.c_str() );*/ \
            crawl_state.add_startup_error(                              \
                make_stringf("Bad %s -- %s\n",                          \
                    key.c_str(), field.c_str()));                       \
        }                                                               \
    } while (false)
#define COLOUR_OPTION(_opt) COLOUR_OPTION_NAMED(#_opt, _opt)

#define CURSES_OPTION_NAMED(_opt_str, _opt_var)     \
    if (key == _opt_str) do {                       \
        this->_opt_var = curses_attribute(field);   \
    } while (false)
#define CURSES_OPTION(_opt) CURSES_OPTION_NAMED(#_opt, _opt)

#define INT_OPTION_NAMED(_opt_str, _opt_var, _min_val, _max_val)        \
    if (key == _opt_str) do {                                           \
        const int min_val = (_min_val);                                 \
        const int max_val = (_max_val);                                 \
        int val = atoi(field.c_str());                                  \
        if (val < min_val) {                                            \
            crawl_state.add_startup_error(                              \
                make_stringf("Bad %s: %d < %d", _opt_str, val, min_val)); \
            val = min_val;                                              \
        } else if (val > max_val) {                                     \
            crawl_state.add_startup_error(                              \
                make_stringf("Bad %s: %d > %d", _opt_str, val, max_val)); \
            val = max_val;                                              \
        }                                                               \
        this->_opt_var = val;                                           \
    } while (false)
#define INT_OPTION(_opt, _min_val, _max_val) \
    INT_OPTION_NAMED(#_opt, _opt, _min_val, _max_val)

    std::string key    = "";
    std::string subkey = "";
    std::string field  = "";

    bool plus_equal  = false;
    bool minus_equal = false;

    const int first_equals = str.find('=');

    // all lines with no equal-signs we ignore
    if (first_equals < 0)
        return;

    field  = str.substr( first_equals + 1 );

    std::string prequal = trimmed_string( str.substr(0, first_equals) );

    // Is this a case of key += val?
    if (prequal.length() && prequal[prequal.length() - 1] == '+')
    {
        plus_equal = true;
        prequal = prequal.substr(0, prequal.length() - 1);
        trim_string(prequal);
    }
    else if (prequal.length() && prequal[prequal.length() - 1] == '-')
    {
        minus_equal = true;
        prequal = prequal.substr(0, prequal.length() - 1);
        trim_string(prequal);
    }
    else if (prequal.length() && prequal[prequal.length() - 1] == ':')
    {
        prequal = prequal.substr(0, prequal.length() - 1);
        trim_string(prequal);
        trim_string(field);

        add_alias(prequal, field);
        return;
    }

    prequal = unalias(prequal);

    const std::string::size_type first_dot = prequal.find('.');
    if (first_dot != std::string::npos)
    {
        key    = prequal.substr( 0, first_dot );
        subkey = prequal.substr( first_dot + 1 );
    }
    else
    {
        // no subkey (dots are okay in value field)
        key    = prequal;
    }

    // Clean up our data...
    lowercase( trim_string( key ) );
    lowercase( trim_string( subkey ) );

    // some fields want capitals... none care about external spaces
    trim_string( field );

    // Keep unlowercased field around
    const std::string orig_field = field;

    if (key != "name" && key != "crawl_dir"
        && key != "race" && key != "class" && key != "ban_pickup"
        && key != "autopickup_exceptions"
        && key != "stop_travel" && key != "sound"
        && key != "travel_stop_message"
        && key != "drop_filter" && key != "lua_file"
        && key != "note_items" && key != "autoinscribe"
        && key != "note_monsters" && key != "note_messages"
        && key.find("cset") != 0 && key != "dungeon"
        && key != "feature" && key != "fire_items_start"
        && key != "mon_glyph" && key != "opt" && key != "option"
        && key != "menu_colour" && key != "menu_color"
        && key != "message_colour" && key != "message_color"
        && key != "levels" && key != "level" && key != "entries")
    {
        lowercase( field );
    }

    if (key == "opt" || key == "option")
    {
        split_parse(field, ",", &game_options::set_option_fragment);
    }
    else if (key == "autopickup")
    {
        // clear out autopickup
        autopickups = 0L;

        for (size_t i = 0; i < field.length(); i++)
        {
            char type = field[i];

            // Make the amulet symbol equiv to ring -- bwross
            switch (type)
            {
            case '"':
                // also represents jewellery
                type = '=';
                break;

            case '|':
                // also represents staves
                type = '\\';
                break;

            case ':':
                // also represents books
                type = '+';
                break;

            case '&':
            case 'x':
                // also corpses
                type = 'X';
                break;
            }

            int j;
            for (j = 0; j < obj_syms_len && type != obj_syms[j]; j++)
                ;

            if (j < obj_syms_len)
                autopickups |= (1L << j);
            else
            {
                crawl_state.add_startup_error(
                    make_stringf("Bad object type '%c' for autopickup.\n",
                                 type));
            }
        }
    }
#if !defined(DGAMELAUNCH) || defined(DGL_REMEMBER_NAME)
    else if (key == "name")
    {
        // field is already cleaned up from trim_string()
        player_name = field;
    }
#endif
#ifndef USE_TILE
    else if (key == "char_set" || key == "ascii_display")
    {
        bool valid = true;

        if (key == "ascii_display")
        {
            char_set =
                _read_bool(field, char_set == CSET_ASCII)?
                    CSET_ASCII
                  : CSET_IBM;
            valid = true;
        }
        else
        {
            if (field == "ascii")
                char_set = CSET_ASCII;
            else if (field == "ibm")
                char_set = CSET_IBM;
            else if (field == "dec")
                char_set = CSET_DEC;
            else if (field == "utf" || field == "unicode")
                char_set = CSET_UNICODE;
            else
            {
                fprintf( stderr, "Bad character set: %s\n", field.c_str() );
                valid = false;
            }
        }
    }
#endif
    else BOOL_OPTION(use_old_selection_order);
    else BOOL_OPTION_NAMED("default_autopickup", autopickup_on);
    else BOOL_OPTION_NAMED("default_autoprayer", autoprayer_on);
    else if (key == "default_friendly_pickup")
    {
        if (field == "none")
            default_friendly_pickup = FRIENDLY_PICKUP_NONE;
        else if (field == "friend")
            default_friendly_pickup = FRIENDLY_PICKUP_FRIEND;
        else if (field == "all")
            default_friendly_pickup = FRIENDLY_PICKUP_ALL;
    }
    else BOOL_OPTION(show_inventory_weights);
    else BOOL_OPTION(suppress_startup_errors);
    else BOOL_OPTION(clean_map);
    else BOOL_OPTION(colour_map);
    else BOOL_OPTION_NAMED("color_map", colour_map);  // common misspelling :)
    else if (key == "easy_confirm")
    {
        // allows both 'Y'/'N' and 'y'/'n' on yesno() prompts
        if (field == "none")
            easy_confirm = CONFIRM_NONE_EASY;
        else if (field == "safe")
            easy_confirm = CONFIRM_SAFE_EASY;
    }
    else BOOL_OPTION(easy_quit_item_prompts);
    else BOOL_OPTION_NAMED("easy_quit_item_lists", easy_quit_item_prompts);
    else BOOL_OPTION(easy_open);
    else BOOL_OPTION(easy_unequip);
    else BOOL_OPTION_NAMED("easy_armour", easy_unequip);
    else BOOL_OPTION_NAMED("easy_armor", easy_unequip);
    else BOOL_OPTION(easy_butcher);
    else BOOL_OPTION(always_confirm_butcher);
    else BOOL_OPTION(list_rotten);
    else if (key == "lua_file" && runscript)
    {
#ifdef CLUA_BINDINGS
        clua.execfile(field.c_str(), false, false);
        if (!clua.error.empty())
            mprf(MSGCH_WARN, "Lua error: %s\n", clua.error.c_str());
#endif
    }
    else if (key == "colour" || key == "color")
    {
        const int orig_col   = str_to_colour( subkey );
        const int result_col = str_to_colour( field );

        if (orig_col != -1 && result_col != -1)
            colour[orig_col] = result_col;
        else
        {
            fprintf( stderr, "Bad colour -- %s=%d or %s=%d\n",
                     subkey.c_str(), orig_col, field.c_str(), result_col );
        }
    }
    else if (key == "channel")
    {
        const int chnl = str_to_channel( subkey );
        const int col  = _str_to_channel_colour( field );

        if (chnl != -1 && col != -1)
            channels[chnl] = col;
        else if (chnl == -1)
            fprintf( stderr, "Bad channel -- %s\n", subkey.c_str() );
        else if (col == -1)
            fprintf( stderr, "Bad colour -- %s\n", field.c_str() );
    }
    else COLOUR_OPTION(background);
    else COLOUR_OPTION(detected_item_colour);
    else COLOUR_OPTION(detected_monster_colour);
    else if (key.find(interrupt_prefix) == 0)
    {
        set_activity_interrupt(key.substr(interrupt_prefix.length()),
                               field,
                               plus_equal,
                               minus_equal);
    }
    else if (key.find("cset") == 0)
    {
        std::string cset = key.substr(4);
        if (!cset.empty() && cset[0] == '_')
            cset = cset.substr(1);

        char_set_type cs = NUM_CSET;
        if (cset == "ascii")
            cs = CSET_ASCII;
        else if (cset == "ibm")
            cs = CSET_IBM;
        else if (cset == "dec")
            cs = CSET_DEC;
        else if (cset == "utf" || cset == "unicode")
            cs = CSET_UNICODE;

        add_cset_override(cs, field);
    }
    else if (key == "feature" || key == "dungeon")
    {
        split_parse(field, ";", &game_options::add_feature_override);
    }
    else if (key == "mon_glyph")
    {
        split_parse(field, ",", &game_options::add_mon_glyph_override);
    }
    else CURSES_OPTION(friend_brand);
    else CURSES_OPTION(neutral_brand);
    else CURSES_OPTION(stab_brand);
    else CURSES_OPTION(may_stab_brand);
    else CURSES_OPTION_NAMED("stair_item_brand", feature_item_brand);
    else CURSES_OPTION(trap_item_brand);
    // This is useful for terms where dark grey does
    // not have standout modes (since it's black on black).
    // This option will use light-grey instead in these cases.
    else BOOL_OPTION(no_dark_brand);
    // no_dark_brand applies here as well.
    else CURSES_OPTION(heap_brand);
    else COLOUR_OPTION(status_caption_colour);
    else if (key == "weapon")
    {
        // choose this weapon for classes that get choice
        weapon = _str_to_weapon( field );
    }
    else if (key == "book")
    {
        // choose this book for classes that get choice
        book = _str_to_book( field );
    }
    else if (key == "chaos_knight")
    {
        // choose god for Chaos Knights
        if (field == "xom")
            chaos_knight = GOD_XOM;
        else if (field == "makhleb")
            chaos_knight = GOD_MAKHLEB;
        else if (field == "random")
            chaos_knight = GOD_RANDOM;
    }
    else if (key == "death_knight")
    {
        if (field == "necromancy")
            death_knight = DK_NECROMANCY;
        else if (field == "yredelemnul")
            death_knight = DK_YREDELEMNUL;
        else if (field == "random")
            death_knight = DK_RANDOM;
    }
    else if (key == "priest")
    {
        // choose this weapon for classes that get choice
        priest = str_to_god(field);
        if (!is_priest_god(priest))
            priest = GOD_RANDOM;
    }
    else if (key == "fire_items_start")
    {
        if (isalpha( field[0] ))
            fire_items_start = letter_to_index( field[0] );
        else
        {
            fprintf( stderr, "Bad fire item start index: %s\n",
                     field.c_str() );
        }
    }
    else if (key == "assign_item_slot")
    {
        if (field == "forward")
            assign_item_slot = SS_FORWARD;
        else if (field == "backward")
            assign_item_slot = SS_BACKWARD;
    }
    else if (key == "fire_order")
    {
        set_fire_order(field, plus_equal);
    }

    BOOL_OPTION(random_pick);
    else BOOL_OPTION(remember_name);
#ifndef SAVE_DIR_PATH
    else if (key == "save_dir")
    {
        // If SAVE_DIR_PATH was defined, there are very likely security issues
        // with allowing the user to specify a different directory.
        save_dir = field;
    }
#endif
    else BOOL_OPTION(show_gold_turns);
    else BOOL_OPTION(show_beam);
#ifndef SAVE_DIR_PATH
    else if (key == "morgue_dir")
    {
        morgue_dir = field;
    }
#endif
    else if (key == "hp_warning")
    {
        hp_warning = atoi( field.c_str() );
        if (hp_warning < 0 || hp_warning > 100)
        {
            hp_warning = 0;
            fprintf( stderr, "Bad HP warning percentage -- %s\n",
                     field.c_str() );
        }
    }
    else if (key == "mp_warning")
    {
        magic_point_warning = atoi( field.c_str() );
        if (magic_point_warning < 0 || magic_point_warning > 100)
        {
            magic_point_warning = 0;
            fprintf( stderr, "Bad MP warning percentage -- %s\n",
                     field.c_str() );
        }
    }
    else if (key == "ood_interesting")
    {
        ood_interesting = atoi( field.c_str() );
    }
    else if (key == "note_monsters")
    {
        append_vector(note_monsters, split_string(",", field));
    }
    else if (key == "note_messages")
    {
        append_vector(note_messages, split_string(",", field));
    }
    else if (key == "note_hp_percent")
    {
        note_hp_percent = atoi( field.c_str() );
        if (note_hp_percent < 0 || note_hp_percent > 100)
        {
            note_hp_percent = 0;
            fprintf( stderr, "Bad HP note percentage -- %s\n",
                     field.c_str() );
        }
    }
    else if (key == "crawl_dir")
    {
        // We shouldn't bother to allocate this a second time
        // if the user puts two crawl_dir lines in the init file.
        SysEnv.crawl_dir = field;
    }
    else if (key == "race")
    {
        race = _str_to_race( field );
    }
    else if (key == "class")
    {
        cls = _str_to_class( field );
    }
    else BOOL_OPTION(auto_list);
    else if (key == "default_target")
    {
        default_target = _read_bool( field, default_target );
        if (default_target)
            target_unshifted_dirs = false;
    }
    else BOOL_OPTION(autopickup_no_burden);
#ifdef DGL_SIMPLE_MESSAGING
    else BOOL_OPTION(messaging);
#endif
    else BOOL_OPTION(mouse_input);
    else if (key == "view_max_width")
    {
        view_max_width = atoi(field.c_str());
        if (view_max_width < VIEW_MIN_WIDTH)
            view_max_width = VIEW_MIN_WIDTH;

        // Allow the view to be one larger than GXM because the view width
        // needs to be odd, and GXM is even.
        else if (view_max_width > GXM + 1)
            view_max_width = GXM + 1;
    }
    else if (key == "view_max_height")
    {
        view_max_height = atoi(field.c_str());
        if (view_max_height < VIEW_MIN_HEIGHT)
            view_max_height = VIEW_MIN_HEIGHT;
        else if (view_max_height > GYM + 1)
            view_max_height = GYM + 1;
    }
    else INT_OPTION(mlist_min_height, 0, INT_MAX);
    else INT_OPTION(msg_max_height, 6, INT_MAX);
    else BOOL_OPTION(mlist_allow_alternate_layout);
    else BOOL_OPTION(classic_hud);
    else BOOL_OPTION(view_lock_x);
    else BOOL_OPTION(view_lock_y);
    else if (key == "view_lock")
    {
        const bool lock = _read_bool(field, true);
        view_lock_x = view_lock_y = lock;
    }
    else BOOL_OPTION(center_on_scroll);
    else BOOL_OPTION(symmetric_scroll);
    else if (key == "scroll_margin_x")
    {
        scroll_margin_x = atoi(field.c_str());
        if (scroll_margin_x < 0)
            scroll_margin_x = 0;
    }
    else if (key == "scroll_margin_y")
    {
        scroll_margin_y = atoi(field.c_str());
        if (scroll_margin_y < 0)
            scroll_margin_y = 0;
    }
    else if (key == "scroll_margin")
    {
        int scrollmarg = atoi(field.c_str());
        if (scrollmarg < 0)
            scrollmarg = 0;
        scroll_margin_x = scroll_margin_y = scrollmarg;
    }
    else if (key == "user_note_prefix")
    {
        // field is already cleaned up from trim_string()
        user_note_prefix = field;
    }
    else BOOL_OPTION(note_skill_max);
    else BOOL_OPTION(note_all_spells);
    else BOOL_OPTION(delay_message_clear);
    else if (key == "flush")
    {
        if (subkey == "failure")
        {
            flush_input[FLUSH_ON_FAILURE]
                = _read_bool(field, flush_input[FLUSH_ON_FAILURE]);
        }
        else if (subkey == "command")
        {
            flush_input[FLUSH_BEFORE_COMMAND]
                = _read_bool(field, flush_input[FLUSH_BEFORE_COMMAND]);
        }
        else if (subkey == "message")
        {
            flush_input[FLUSH_ON_MESSAGE]
                = _read_bool(field, flush_input[FLUSH_ON_MESSAGE]);
        }
        else if (subkey == "lua")
        {
            flush_input[FLUSH_LUA]
                = _read_bool(field, flush_input[FLUSH_LUA]);
        }
    }
    else if (key == "wiz_mode")
    {
        // wiz_mode is recognized as a legal key in all compiles -- bwr
#ifdef WIZARD
        if (field == "never")
            wiz_mode = WIZ_NEVER;
        else if (field == "no")
            wiz_mode = WIZ_NO;
        else if (field == "yes")
            wiz_mode = WIZ_YES;
        else
        {
            crawl_state.add_startup_error(
                make_stringf("Unknown wiz_mode option: %s\n", field.c_str()));
        }
#endif
    }
    else if (key == "ban_pickup")
    {
        append_vector(never_pickup, split_string(",", field));
    }
    else if (key == "autopickup_exceptions")
    {
        std::vector<std::string> args = split_string(",", field);
        for (int i = 0, size = args.size(); i < size; ++i)
        {
            const std::string &s = args[i];
            if (s.empty())
                continue;

            if (s[0] == '>')
                never_pickup.push_back( s.substr(1) );
            else if (s[0] == '<')
                always_pickup.push_back( s.substr(1) );
            else
                never_pickup.push_back( s );
        }
    }
    else if (key == "note_items")
    {
        append_vector(note_items, split_string(",", field));
    }
    else if (key == "autoinscribe")
    {
        std::vector<std::string> thesplit = split_string(":", field);
        autoinscriptions.push_back(
            std::pair<text_pattern,std::string>(thesplit[0], thesplit[1]));
    }
    else if (key == "map_file_name")
    {
        map_file_name = field;
    }
    else if (key == "hp_colour" || key == "hp_color")
    {
        hp_colour.clear();
        std::vector<std::string> thesplit = split_string(",", field);
        for ( unsigned i = 0; i < thesplit.size(); ++i )
        {
            std::vector<std::string> insplit = split_string(":", thesplit[i]);
            int hp_percent = 100;

            if ( insplit.size() == 0 || insplit.size() > 2
                 || insplit.size() == 1 && i != 0 )
            {
                crawl_state.add_startup_error(
                    make_stringf("Bad hp_colour string: %s\n", field.c_str()));
                break;
            }

            if ( insplit.size() == 2 )
                hp_percent = atoi(insplit[0].c_str());

            int scolour = str_to_colour(insplit[(insplit.size() == 1) ? 0 : 1]);
            hp_colour.push_back(std::pair<int, int>(hp_percent, scolour));
        }
    }
    else if (key == "mp_color" || key == "mp_colour")
    {
        mp_colour.clear();
        std::vector<std::string> thesplit = split_string(",", field);
        for ( unsigned i = 0; i < thesplit.size(); ++i )
        {
            std::vector<std::string> insplit = split_string(":", thesplit[i]);
            int mp_percent = 100;

            if ( insplit.size() == 0 || insplit.size() > 2
                 || insplit.size() == 1 && i != 0 )
            {
                crawl_state.add_startup_error(
                    make_stringf("Bad mp_colour string: %s\n", field.c_str()));
                break;
            }

            if ( insplit.size() == 2 )
                mp_percent = atoi(insplit[0].c_str());

            int scolour = str_to_colour(insplit[(insplit.size() == 1) ? 0 : 1]);
            mp_colour.push_back(std::pair<int, int>(mp_percent, scolour));
        }
    }
    else if (key == "note_skill_levels")
    {
        std::vector<std::string> thesplit = split_string(",", field);
        for ( unsigned i = 0; i < thesplit.size(); ++i )
        {
            int num = atoi(thesplit[i].c_str());
            if ( num > 0 && num <= 27 )
                note_skill_levels.push_back(num);
            else
            {
                crawl_state.add_startup_error(
                    make_stringf("Bad skill level to note -- %s\n",
                                 thesplit[i].c_str()));
                continue;
            }
        }
    }

    BOOL_OPTION(pickup_thrown);
    else BOOL_OPTION(pickup_dropped);
#ifdef WIZARD
    else if (key == "fsim_kit")
    {
        append_vector(fsim_kit, split_string(",", field));
    }
    else if (key == "fsim_rounds")
    {
        fsim_rounds = atol(field.c_str());
        if (fsim_rounds < 1000)
            fsim_rounds = 1000;
        if (fsim_rounds > 500000L)
            fsim_rounds = 500000L;
    }
    else if (key == "fsim_mons")
    {
        fsim_mons = field;
    }
    else if (key == "fsim_str")
    {
        fsim_str = atoi(field.c_str());
    }
    else if (key == "fsim_int")
    {
        fsim_int = atoi(field.c_str());
    }
    else if (key == "fsim_dex")
    {
        fsim_dex = atoi(field.c_str());
    }
    else if (key == "fsim_xl")
    {
        fsim_xl = atoi(field.c_str());
    }
#endif // WIZARD
    else if (key == "sort_menus")
    {
        std::vector<std::string> frags = split_string(";", field);
        for (int i = 0, size = frags.size(); i < size; ++i)
        {
            if (frags[i].empty())
                continue;
            set_menu_sort(frags[i]);
        }
    }
    else if (key == "travel_delay")
    {
        // Read travel delay in milliseconds.
        travel_delay = atoi( field.c_str() );
        if (travel_delay < -1)
            travel_delay = -1;
        if (travel_delay > 2000)
            travel_delay = 2000;
    }
    else if (key == "level_map_cursor_step")
    {
        level_map_cursor_step = atoi( field.c_str() );
        if (level_map_cursor_step < 1)
            level_map_cursor_step = 1;
        if (level_map_cursor_step > 50)
            level_map_cursor_step = 50;
    }
    else BOOL_OPTION(use_fake_cursor);
    else BOOL_OPTION(macro_meta_entry);
    else if (key == "stop_travel" || key == "travel_stop_message")
    {
        std::vector<std::string> fragments = split_string(",", field);
        for (int i = 0, count = fragments.size(); i < count; ++i)
        {
            if (fragments[i].length() == 0)
                continue;

            std::string::size_type pos = fragments[i].find(":");
            if (pos && pos != std::string::npos)
            {
                std::string prefix = fragments[i].substr(0, pos);
                int channel = str_to_channel( prefix );
                if (channel != -1 || prefix == "any")
                {
                    std::string s = fragments[i].substr( pos + 1 );
                    trim_string( s );
                    travel_stop_message.push_back(
                       message_filter( channel, s ) );
                    continue;
                }
            }

            travel_stop_message.push_back(
                    message_filter( fragments[i] ) );
        }
    }
    else if (key == "drop_filter")
    {
        append_vector(drop_filter, split_string(",", field));
    }
    else if (key == "travel_avoid_terrain")
    {
        std::vector<std::string> seg = split_string(",", field);
        for (int i = 0, count = seg.size(); i < count; ++i)
            prevent_travel_to( seg[i] );
    }
    else if (key == "tc_reachable")
    {
        tc_reachable = str_to_colour(field, tc_reachable);
    }
    else if (key == "tc_excluded")
    {
        tc_excluded = str_to_colour(field, tc_excluded);
    }
    else if (key == "tc_exclude_circle")
    {
        tc_exclude_circle =
            str_to_colour(field, tc_exclude_circle);
    }
    else if (key == "tc_dangerous")
    {
        tc_dangerous = str_to_colour(field, tc_dangerous);
    }
    else if (key == "tc_disconnected")
    {
        tc_disconnected = str_to_colour(field, tc_disconnected);
    }
    else BOOL_OPTION(classic_item_colours);
    else BOOL_OPTION(item_colour);
    else BOOL_OPTION_NAMED("item_color", item_colour);
    else BOOL_OPTION(easy_exit_menu);
    else BOOL_OPTION(dos_use_background_intensity);
    else if (key == "item_stack_summary_minimum")
    {
        item_stack_summary_minimum = atoi(field.c_str());
    }
    else if (key == "explore_stop")
    {
        if (!plus_equal && !minus_equal)
            explore_stop = ES_NONE;

        const int new_conditions = read_explore_stop_conditions(field);
        if (minus_equal)
            explore_stop &= ~new_conditions;
        else
            explore_stop |= new_conditions;
    }
    else if (key == "explore_stop_prompt")
    {
        if (!plus_equal && !minus_equal)
            explore_stop_prompt = ES_NONE;
        const int new_conditions = read_explore_stop_conditions(field);
        if (minus_equal)
            explore_stop_prompt &= ~new_conditions;
        else
            explore_stop_prompt |= new_conditions;
    }
    else if (key == "explore_item_greed")
    {
        explore_item_greed = atoi( field.c_str() );
        if (explore_item_greed > 1000)
            explore_item_greed = 1000;
        else if (explore_item_greed < -1000)
            explore_item_greed = -1000;
    }
    else BOOL_OPTION(explore_greedy);
    else BOOL_OPTION(explore_improved);

	BOOL_OPTION(trap_prompt);
	else if (key == "stash_tracking")
    {
        stash_tracking =
             field == "dropped" ? STM_DROPPED  :
             field == "all"     ? STM_ALL      :
                                  STM_EXPLICIT;
    }
    else if (key == "stash_filter")
    {
        std::vector<std::string> seg = split_string(",", field);
        for (int i = 0, count = seg.size(); i < count; ++i)
            Stash::filter( seg[i] );
    }
    else if (key == "sound")
    {
        std::vector<std::string> seg = split_string(",", field);
        for (int i = 0, count = seg.size(); i < count; ++i)
        {
            const std::string &sub = seg[i];
            std::string::size_type cpos = sub.find(":", 0);
            if (cpos != std::string::npos)
            {
                sound_mapping mapping;
                mapping.pattern = sub.substr(0, cpos);
                mapping.soundfile = sub.substr(cpos + 1);
                sound_mappings.push_back(mapping);
            }
        }
    }
    // MSVC has a limit on how many if/else if can be chained together.
    /* else */ if (key == "menu_colour" || key == "menu_color")
    {
        std::vector<std::string> seg = split_string(",", field);
        for (int i = 0, count = seg.size(); i < count; ++i)
        {
            // format: tag:string:colour
            // FIXME: arrange so that you can use ':' inside a pattern
            std::vector<std::string> subseg = split_string(":", seg[i], false);
            std::string tagname, patname, colname;
            if ( subseg.size() < 2 )
                continue;
            if ( subseg.size() >= 3 )
            {
                tagname = subseg[0];
                colname = subseg[1];
                patname = subseg[2];
            }
            else
            {
                colname = subseg[0];
                patname = subseg[1];
            }

            colour_mapping mapping;
            mapping.tag     = tagname;
            mapping.pattern = patname;
            mapping.colour  = str_to_colour(colname);

            if (mapping.colour != -1)
                menu_colour_mappings.push_back(mapping);
        }
    }
    else BOOL_OPTION(menu_colour_prefix_class);
    else BOOL_OPTION_NAMED("menu_color_prefix_class", menu_colour_prefix_class);
    else BOOL_OPTION(menu_colour_prefix_id);
    else BOOL_OPTION_NAMED("menu_color_prefix_id", menu_colour_prefix_id);
    else if (key == "message_colour" || key == "message_color")
    {
        add_message_colour_mappings(field);
    }
    else if (key == "dump_order")
    {
        if (!plus_equal)
            dump_order.clear();

        new_dump_fields(field, !minus_equal);
    }
    else if (key == "dump_kill_places")
    {
        dump_kill_places = (field == "none" ? KDO_NO_PLACES :
                            field == "all"  ? KDO_ALL_PLACES
                                            : KDO_ONE_PLACE);
    }
    else if (key == "kill_map")
    {
        std::vector<std::string> seg = split_string(",", field);
        for (int i = 0, count = seg.size(); i < count; ++i)
        {
            const std::string &s = seg[i];
            std::string::size_type cpos = s.find(":", 0);
            if (cpos != std::string::npos)
            {
                std::string from = s.substr(0, cpos);
                std::string to   = s.substr(cpos + 1);
                do_kill_map(from, to);
            }
        }
    }
    else if (key == "dump_message_count")
    {
        // Capping is implicit
        dump_message_count = atoi( field.c_str() );
    }
    else if (key == "dump_item_origins")
    {
        dump_item_origins = IODS_PRICE;
        std::vector<std::string> choices = split_string(",", field);
        for (int i = 0, count = choices.size(); i < count; ++i)
        {
            const std::string &ch = choices[i];
            if (ch == "artefacts" || ch == "artifacts"
                    || ch == "artefact" || ch == "artifact")
                dump_item_origins |= IODS_ARTEFACTS;
            else if (ch == "ego_arm" || ch == "ego armour"
                    || ch == "ego_armour")
                dump_item_origins |= IODS_EGO_ARMOUR;
            else if (ch == "ego_weap" || ch == "ego weapon"
                    || ch == "ego_weapon" || ch == "ego weapons"
                    || ch == "ego_weapons")
                dump_item_origins |= IODS_EGO_WEAPON;
            else if (ch == "jewellery" || ch == "jewelry")
                dump_item_origins |= IODS_JEWELLERY;
            else if (ch == "runes")
                dump_item_origins |= IODS_RUNES;
            else if (ch == "rods")
                dump_item_origins |= IODS_RODS;
            else if (ch == "staves")
                dump_item_origins |= IODS_STAVES;
            else if (ch == "books")
                dump_item_origins |= IODS_BOOKS;
            else if (ch == "all" || ch == "everything")
                dump_item_origins = IODS_EVERYTHING;
        }
    }
    else if (key == "dump_item_origin_price")
    {
        dump_item_origin_price = atoi( field.c_str() );
        if (dump_item_origin_price < -1)
            dump_item_origin_price = -1;
    }
    else BOOL_OPTION(level_map_title);
    else BOOL_OPTION(target_zero_exp);
    else BOOL_OPTION(target_oos);
    else BOOL_OPTION(target_los_first);
    else if (key == "target_unshifted_dirs")
    {
        target_unshifted_dirs = _read_bool(field, target_unshifted_dirs);
        if (target_unshifted_dirs)
            default_target = false;
    }
    else if (key == "drop_mode")
    {
        if (field.find("multi") != std::string::npos)
            drop_mode = DM_MULTI;
        else
            drop_mode = DM_SINGLE;
    }
    else if (key == "pickup_mode")
    {
        if (field.find("multi") != std::string::npos)
            pickup_mode = 0;
        else if (field.find("single") != std::string::npos)
            pickup_mode = -1;
        else
            pickup_mode = _read_bool_or_number(field, pickup_mode, "auto:");
    }
    else if (key == "additional_macro_file")
    {
        additional_macro_files.push_back(orig_field);
    }
#ifdef USE_TILE
    else if (key == "tile_show_items")
    {
        strncpy(tile_show_items, field.c_str(), 18);
    }
    else BOOL_OPTION(tile_title_screen);
    else if (key == "tile_player_col")
    {
        tile_player_col =
               _str_to_tile_colour(field);
    }
    else if (key == "tile_monster_col")
    {
        tile_monster_col =
               _str_to_tile_colour(field);
    }
    else if (key == "tile_neutral_col")
    {
        tile_neutral_col =
               _str_to_tile_colour(field);
    }
    else if (key == "tile_friendly_col")
    {
        tile_friendly_col =
               _str_to_tile_colour(field);
    }
    else if (key == "tile_plant_col")
    {
        tile_plant_col =
               _str_to_tile_colour(field);
    }
    else if (key == "tile_item_col")
    {
        tile_item_col =
               _str_to_tile_colour(field);
    }
    else if (key == "tile_unseen_col")
    {
        tile_unseen_col =
               _str_to_tile_colour(field);
    }
    else if (key == "tile_floor_col")
    {
        tile_floor_col =
               _str_to_tile_colour(field);
    }
    else if (key == "tile_wall_col")
    {
        tile_wall_col =
               _str_to_tile_colour(field);
    }
    else if (key == "tile_mapped_wall_col")
    {
        tile_mapped_wall_col =
               _str_to_tile_colour(field);
    }
    else if (key == "tile_door_col")
    {
        tile_door_col =
               _str_to_tile_colour(field);
    }
    else if (key == "tile_downstairs_col")
    {
        tile_downstairs_col =
               _str_to_tile_colour(field);
    }
    else if (key == "tile_upstairs_col")
    {
        tile_upstairs_col =
               _str_to_tile_colour(field);
    }
    else if (key == "tile_feature_col")
    {
        tile_feature_col =
               _str_to_tile_colour(field);
    }
    else if (key == "tile_trap_col")
    {
        tile_trap_col =
               _str_to_tile_colour(field);
    }
    else if (key == "tile_water_col")
    {
        tile_water_col =
               _str_to_tile_colour(field);
    }
    else if (key == "tile_lava_col")
    {
        tile_lava_col =
               _str_to_tile_colour(field);
    }
    else if (key == "tile_excluded_col")
    {
        tile_excluded_col =
               _str_to_tile_colour(field);
    }
    else if (key == "tile_excl_centre_col" || key == "tile_excl_center_col")
    {
        tile_excl_centre_col =
               _str_to_tile_colour(field);
    }
#endif

#ifdef WIN32TILES
    else BOOL_OPTION(use_dos_char);
#endif

    // Catch-all else, copies option into map
    else if (runscript)
    {
#ifdef CLUA_BINDINGS
        if (!clua.callbooleanfn(false, "c_process_lua_option", "ss",
                        key.c_str(), orig_field.c_str()))
#endif
        {
#ifdef CLUA_BINDINGS
            if (!clua.error.empty())
                mprf(MSGCH_WARN, "Lua error: %s\n", clua.error.c_str());
#endif
            named_options[key] = orig_field;
        }
    }
}

static std::string check_string(const char *s)
{
    return (s? s : "");
}

void get_system_environment(void)
{
    // The player's name
    SysEnv.crawl_name = check_string( getenv("CRAWL_NAME") );

    // The player's pizza
    SysEnv.crawl_pizza = check_string( getenv("CRAWL_PIZZA") );

    // The directory which contians init.txt, macro.txt, morgue.txt
    // This should end with the appropriate path delimiter.
    SysEnv.crawl_dir = check_string( getenv("CRAWL_DIR") );

#ifdef DGL_SIMPLE_MESSAGING
    // Enable DGL_SIMPLE_MESSAGING only if SIMPLEMAIL and MAIL are set.
    const char *simplemail = getenv("SIMPLEMAIL");
    if (simplemail && strcmp(simplemail, "0"))
    {
        const char *mail = getenv("MAIL");
        SysEnv.messagefile = mail? mail : "";
    }
#endif

    // The full path to the init file -- this over-rides CRAWL_DIR
    SysEnv.crawl_rc = check_string( getenv("CRAWL_RC") );

    // rename giant and giant spiked clubs
    SysEnv.board_with_nail = (getenv("BOARD_WITH_NAIL") != NULL);

#ifdef MULTIUSER
    // The user's home directory (used to look for ~/.crawlrc file)
    SysEnv.home = check_string( getenv("HOME") );
#endif
}                               // end get_system_environment()

static void set_crawl_base_dir(const char *arg)
{
    if (!arg)
        return;

    SysEnv.crawl_base = get_parent_directory(arg);
}

// parse args, filling in Options and game environment as we go.
// returns true if no unknown or malformed arguments were found.

// Keep this in sync with the option names.
enum commandline_option_type {
    CLO_SCORES,
    CLO_NAME,
    CLO_RACE,
    CLO_CLASS,
    CLO_PIZZA,
    CLO_PLAIN,
    CLO_DIR,
    CLO_RC,
    CLO_TSCORES,
    CLO_VSCORES,
    CLO_SCOREFILE,
    CLO_MORGUE,
    CLO_MACRO,
    CLO_MAPSTAT,

    CLO_NOPS
};

static const char *cmd_ops[] = { "scores", "name", "race", "class",
                                 "pizza", "plain", "dir", "rc", "tscores",
                                 "vscores", "scorefile", "morgue",
                                 "macro", "mapstat" };

const int num_cmd_ops = CLO_NOPS;
bool arg_seen[num_cmd_ops];

bool parse_args( int argc, char **argv, bool rc_only )
{
    set_crawl_base_dir(argv[0]);

    if (argc < 2)           // no args!
        return (true);

    char *arg, *next_arg;
    int current = 1;
    bool nextUsed = false;
    int ecount;

    // initialize
    for (int i = 0; i < num_cmd_ops; i++)
         arg_seen[i] = false;

    if (SysEnv.cmd_args.empty())
        for (int i = 1; i < argc; ++i)
            SysEnv.cmd_args.push_back(argv[i]);

    while (current < argc)
    {
        // get argument
        arg = argv[current];

        // next argument (if there is one)
        if (current+1 < argc)
            next_arg = argv[current+1];
        else
            next_arg = NULL;

        nextUsed = false;

        // arg MUST begin with '-'
        char c = arg[0];
        if (c != '-')
        {
            fprintf(stderr,
                    "Option '%s' is invalid; options must be prefixed "
                    "with -\n\n", arg);
            return (false);
        }

        // look for match (now we also except --scores)
        if (arg[1] == '-')
            arg = &arg[2];
        else
            arg = &arg[1];

        int o;
        for (o = 0; o < num_cmd_ops; o++)
        {
             if (stricmp(cmd_ops[o], arg) == 0)
                 break;
        }

        if (o == num_cmd_ops)
        {
            fprintf(stderr,
                    "Unknown option: %s\n\n", argv[current]);
            return (false);
        }

        // disallow options specified more than once.
        if (arg_seen[o] == true)
            return (false);

        // set arg to 'seen'
        arg_seen[o] = true;

        // partially parse next argument
        bool next_is_param = false;
        if (next_arg != NULL)
        {
            if (next_arg[0] != '-' || strlen(next_arg) == 1)
                next_is_param = true;
        }

        //.take action according to the cmd chosen
        switch(o)
        {
        case CLO_SCORES:
        case CLO_TSCORES:
        case CLO_VSCORES:
            if (!next_is_param)
                ecount = -1;            // default
            else // optional number given
            {
                ecount = atoi(next_arg);
                if (ecount < 1)
                    ecount = 1;

                if (ecount > SCORE_FILE_ENTRIES)
                    ecount = SCORE_FILE_ENTRIES;

                nextUsed = true;
            }

            if (!rc_only)
            {
                Options.sc_entries = ecount;

                if (o == CLO_TSCORES)
                    Options.sc_format = SCORE_TERSE;
                else if (o == CLO_VSCORES)
                    Options.sc_format = SCORE_VERBOSE;
                else if (o == CLO_SCORES)
                    Options.sc_format = SCORE_REGULAR;
            }
            break;

        case CLO_MAPSTAT:
            crawl_state.map_stat_gen = true;
            if (next_is_param)
            {
                SysEnv.map_gen_iters = atoi(next_arg);
                if (SysEnv.map_gen_iters < 1)
                    SysEnv.map_gen_iters = 1;
                else if (SysEnv.map_gen_iters > 10000)
                    SysEnv.map_gen_iters = 10000;
                nextUsed = true;
            }
            else
                SysEnv.map_gen_iters = 100;
            break;

        case CLO_MACRO:
            if (!next_is_param)
                return (false);
            if (!rc_only)
                Options.macro_dir = next_arg;
            nextUsed = true;
            break;

        case CLO_MORGUE:
            if (!next_is_param)
                return (false);
            if (!rc_only)
                SysEnv.morgue_dir = next_arg;
            nextUsed = true;
            break;

        case CLO_SCOREFILE:
            if (!next_is_param)
                return (false);
            if (!rc_only)
                SysEnv.scorefile = next_arg;
            nextUsed = true;
            break;

        case CLO_NAME:
            if (!next_is_param)
                return (false);
            if (!rc_only)
                Options.player_name = next_arg;
            nextUsed = true;
            break;

        case CLO_RACE:
        case CLO_CLASS:
            if (!next_is_param)
                return (false);

            if (!rc_only)
            {
                if (o == 2)
                    Options.race = _str_to_race( std::string( next_arg ) );

                if (o == 3)
                    Options.cls = _str_to_class( std::string( next_arg ) );
            }
            nextUsed = true;
            break;

        case CLO_PIZZA:
            if (!next_is_param)
                return (false);

            if (!rc_only)
                SysEnv.crawl_pizza = next_arg;

            nextUsed = true;
            break;

        case CLO_PLAIN:
            if (next_is_param)
                return (false);

            if (!rc_only)
            {
                Options.char_set = CSET_ASCII;
                init_char_table(Options.char_set);
            }
            break;

        case CLO_DIR:
            // ALWAYS PARSE
            if (!next_is_param)
                return (false);

            SysEnv.crawl_dir = next_arg;
            nextUsed = true;
            break;

        case CLO_RC:
            // ALWAYS PARSE
            if (!next_is_param)
                return (false);

            SysEnv.crawl_rc = next_arg;
            nextUsed = true;
            break;
        } // end switch -- which option?

        // update position
        current++;
        if (nextUsed)
            current++;
    }

    return (true);
}

//////////////////////////////////////////////////////////////////////////
// game_options

int game_options::o_int(const char *name, int def) const
{
    int val = def;
    opt_map::const_iterator i = named_options.find(name);
    if (i != named_options.end())
    {
        val = atoi(i->second.c_str());
    }
    return (val);
}

long game_options::o_long(const char *name, long def) const
{
    long val = def;
    opt_map::const_iterator i = named_options.find(name);
    if (i != named_options.end())
    {
        const char *s = i->second.c_str();
        char *es = NULL;
        long num = strtol(s, &es, 10);
        if (s != (const char *) es && es)
            val = num;
    }
    return (val);
}

bool game_options::o_bool(const char *name, bool def) const
{
    bool val = def;
    opt_map::const_iterator i = named_options.find(name);
    if (i != named_options.end())
        val = _read_bool(i->second, val);
    return (val);
}

std::string game_options::o_str(const char *name, const char *def) const
{
    std::string val;
    opt_map::const_iterator i = named_options.find(name);
    if (i != named_options.end())
        val = i->second;
    else if (def)
        val = def;
    return (val);
}

int game_options::o_colour(const char *name, int def) const
{
    std::string val = o_str(name);
    trim_string(val);
    lowercase(val);
    int col = str_to_colour(val);
    return (col == -1? def : col);
}

///////////////////////////////////////////////////////////////////////
// menu_sort_condition

menu_sort_condition::menu_sort_condition(menu_type _mt, int _sort)
    : mtype(_mt), sort(_sort), cmp()
{
}

menu_sort_condition::menu_sort_condition(const std::string &s)
    : mtype(MT_ANY), sort(-1), cmp()
{
    std::string cp = s;
    set_menu_type(cp);
    set_sort(cp);
    set_comparators(cp);
}

bool menu_sort_condition::matches(menu_type mt) const
{
    return (mtype == MT_ANY || mtype == mt);
}

void menu_sort_condition::set_menu_type(std::string &s)
{
    static struct
    {
        const std::string mname;
        menu_type mtype;
    } menu_type_map[] =
      {
          { "any:",    MT_ANY       },
          { "inv:",    MT_INVLIST   },
          { "drop:",   MT_DROP      },
          { "pickup:", MT_PICKUP    }
      };

    for (unsigned mi = 0; mi < ARRAYSZ(menu_type_map); ++mi)
    {
        const std::string &name = menu_type_map[mi].mname;
        if (s.find(name) == 0)
        {
            s = s.substr(name.length());
            mtype = menu_type_map[mi].mtype;
            break;
        }
    }
}

void menu_sort_condition::set_sort(std::string &s)
{
    // Strip off the optional sort clauses and get the primary sort condition.
    std::string::size_type trail_pos = s.find(':');
    if (s.find("auto:") == 0)
        trail_pos = s.find(':', trail_pos + 1);

    std::string sort_cond =
        trail_pos == std::string::npos? s : s.substr(0, trail_pos);

    trim_string(sort_cond);
    sort = _read_bool_or_number(sort_cond, sort, "auto:");

    if (trail_pos != std::string::npos)
        s = s.substr(trail_pos + 1);
    else
        s.clear();
}

void menu_sort_condition::set_comparators(std::string &s)
{
    init_item_sort_comparators(
        cmp,
        s.empty()? "equipped, basename, qualname, curse, qty" : s);
}
