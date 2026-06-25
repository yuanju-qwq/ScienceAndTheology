class_name MaterialDefinitions
extends RefCounted

# Generation flags — mirrors C++ MaterialGenFlag
const GEN_DUST   := 1
const GEN_METAL  := 2
const GEN_GEM    := 4
const GEN_ORE    := 8
const GEN_CELL   := 16
const GEN_PLASMA := 32
const GEN_WIRE   := 64
const GEN_BLOCK  := 128

const DUST_ONLY  := GEN_DUST
const METAL_FULL := GEN_DUST | GEN_METAL | GEN_BLOCK | GEN_WIRE
const ORE_FULL   := METAL_FULL | GEN_ORE
const GEM_FULL   := GEN_DUST | GEN_GEM | GEN_BLOCK
const FLUID      := GEN_CELL
const GAS        := GEN_CELL

# State — mirrors C++ MaterialState
const SOLID  := 0
const LIQUID := 1
const GASEOUS := 2
const PLASMA := 3

# Register all built-in materials with the GDMaterialRegistry C++ binding.
# Must be called before any terrain content registration.
# 显式确定性 ID：用 _ALL_MATERIALS 数组下标作为 material_id，保持现有 ID 稳定。
static func register_all() -> void:
	for i in range(_ALL_MATERIALS.size()):
		var entry := _ALL_MATERIALS[i].duplicate()
		entry["id"] = i
		GDMaterialRegistry.register_material(entry)

# Register mineral compounds as lightweight mod items (NOT C++ materials).
# These are what ore blocks drop when mined — realistic mineral intermediates
# instead of pure elements. Call AFTER register_all() but BEFORE finalize().
# Each compound needs only a string key and title; no gen_flags, melting point, etc.
static func register_compounds() -> void:
	var _title := "item.compound."
	for entry in _ALL_COMPOUNDS:
		var key := entry[0]
		var title := _title + key.replace(".", "_")
		GDMaterialRegistry.register_compound(key, title)

# Each entry: {name, title_key, gen_flags, state, color,
#              melting_point, boiling_point, mass, chemical_formula, elements}
# elements is an array of {element: String, count: int}
const _ALL_MATERIALS := [
# Primitive / stone-age solids
{name: "stone",    title_key: "material.stone",    gen_flags: DUST_ONLY, state: SOLID, color: 0x808080, melting_point: 0,    boiling_point: 0,   mass: 1.0,  chemical_formula: "?",       elements: []},
{name: "flint",    title_key: "material.flint",    gen_flags: DUST_ONLY, state: SOLID, color: 0x303030, melting_point: 0,    boiling_point: 0,   mass: 1.0,  chemical_formula: "SiO2",     elements: []},
{name: "coal",     title_key: "material.coal",     gen_flags: DUST_ONLY | GEN_GEM, state: SOLID, color: 0x1A1A1A, melting_point: 0,    boiling_point: 0,   mass: 1.0,  chemical_formula: "C",        elements: []},
{name: "charcoal", title_key: "material.charcoal", gen_flags: DUST_ONLY, state: SOLID, color: 0x2A1A0A, melting_point: 0,    boiling_point: 0,   mass: 1.0,  chemical_formula: "C",        elements: []},
{name: "lignite",  title_key: "material.lignite",  gen_flags: DUST_ONLY, state: SOLID, color: 0x3A2A1A, melting_point: 0,    boiling_point: 0,   mass: 1.0,  chemical_formula: "C",        elements: []},

# Basic metals
{name: "copper",    title_key: "material.copper",    gen_flags: ORE_FULL, state: SOLID, color: 0xFF7F24, melting_point: 1358, boiling_point: 2835, mass: 63.5,  chemical_formula: "Cu",  elements: [{element: "Cu", count: 1}]},
{name: "tin",       title_key: "material.tin",       gen_flags: ORE_FULL, state: SOLID, color: 0xE0E0E0, melting_point: 505,  boiling_point: 2875, mass: 118.7, chemical_formula: "Sn",  elements: [{element: "Sn", count: 1}]},
{name: "iron",      title_key: "material.iron",      gen_flags: ORE_FULL, state: SOLID, color: 0xC8B0A0, melting_point: 1811, boiling_point: 3134, mass: 55.8,  chemical_formula: "Fe",  elements: [{element: "Fe", count: 1}]},
{name: "lead",      title_key: "material.lead",      gen_flags: ORE_FULL, state: SOLID, color: 0x7070A0, melting_point: 601,  boiling_point: 2013, mass: 207.2, chemical_formula: "Pb",  elements: [{element: "Pb", count: 1}]},
{name: "silver",    title_key: "material.silver",    gen_flags: ORE_FULL, state: SOLID, color: 0xC0C0E0, melting_point: 1235, boiling_point: 2435, mass: 107.9, chemical_formula: "Ag",  elements: [{element: "Ag", count: 1}]},
{name: "gold",      title_key: "material.gold",      gen_flags: ORE_FULL, state: SOLID, color: 0xFFD700, melting_point: 1337, boiling_point: 3129, mass: 197.0, chemical_formula: "Au",  elements: [{element: "Au", count: 1}]},
{name: "zinc",      title_key: "material.zinc",      gen_flags: ORE_FULL, state: SOLID, color: 0xC0D0C0, melting_point: 693,  boiling_point: 1180, mass: 65.4,  chemical_formula: "Zn",  elements: [{element: "Zn", count: 1}]},
{name: "nickel",    title_key: "material.nickel",    gen_flags: ORE_FULL, state: SOLID, color: 0xA0B0A0, melting_point: 1728, boiling_point: 3186, mass: 58.7,  chemical_formula: "Ni",  elements: [{element: "Ni", count: 1}]},
{name: "aluminium", title_key: "material.aluminium", gen_flags: ORE_FULL, state: SOLID, color: 0xD0E0F0, melting_point: 933,  boiling_point: 2792, mass: 27.0,  chemical_formula: "Al",  elements: [{element: "Al", count: 1}]},
{name: "platinum",  title_key: "material.platinum",  gen_flags: ORE_FULL, state: SOLID, color: 0xE0E0E0, melting_point: 2041, boiling_point: 4098, mass: 195.1, chemical_formula: "Pt",  elements: [{element: "Pt", count: 1}]},
{name: "tungsten",  title_key: "material.tungsten",  gen_flags: ORE_FULL, state: SOLID, color: 0x909090, melting_point: 3695, boiling_point: 6203, mass: 183.8, chemical_formula: "W",   elements: [{element: "W", count: 1}]},
{name: "titanium",  title_key: "material.titanium",  gen_flags: ORE_FULL, state: SOLID, color: 0xC0C0D0, melting_point: 1941, boiling_point: 3560, mass: 47.9,  chemical_formula: "Ti",  elements: [{element: "Ti", count: 1}]},
{name: "chrome",    title_key: "material.chrome",    gen_flags: ORE_FULL, state: SOLID, color: 0xD0D0E0, melting_point: 2180, boiling_point: 2944, mass: 52.0,  chemical_formula: "Cr",  elements: [{element: "Cr", count: 1}]},
{name: "manganese", title_key: "material.manganese", gen_flags: ORE_FULL, state: SOLID, color: 0xC0B0A0, melting_point: 1519, boiling_point: 2334, mass: 54.9,  chemical_formula: "Mn",  elements: [{element: "Mn", count: 1}]},
{name: "cobalt",    title_key: "material.cobalt",    gen_flags: ORE_FULL, state: SOLID, color: 0xB0A0C0, melting_point: 1768, boiling_point: 3200, mass: 58.9,  chemical_formula: "Co",  elements: [{element: "Co", count: 1}]},
{name: "bismuth",   title_key: "material.bismuth",   gen_flags: METAL_FULL, state: SOLID, color: 0xD0A0A0, melting_point: 545,  boiling_point: 1837, mass: 209.0, chemical_formula: "Bi",  elements: [{element: "Bi", count: 1}]},
{name: "antimony",  title_key: "material.antimony",  gen_flags: METAL_FULL, state: SOLID, color: 0xE0E0F0, melting_point: 904,  boiling_point: 1860, mass: 121.8, chemical_formula: "Sb",  elements: [{element: "Sb", count: 1}]},

# Alloys
{name: "bronze",          title_key: "material.bronze",          gen_flags: METAL_FULL, state: SOLID, color: 0xCD7F32, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Cu+Sn",    elements: [{element: "Cu", count: 3}, {element: "Sn", count: 1}]},
{name: "brass",           title_key: "material.brass",           gen_flags: METAL_FULL, state: SOLID, color: 0xC5A542, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Cu+Zn",    elements: [{element: "Cu", count: 3}, {element: "Zn", count: 1}]},
{name: "steel",           title_key: "material.steel",           gen_flags: METAL_FULL, state: SOLID, color: 0x808090, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Fe+C",     elements: [{element: "Fe", count: 1}, {element: "C", count: 1}]},
{name: "stainless_steel", title_key: "material.stainless_steel", gen_flags: METAL_FULL, state: SOLID, color: 0xC0C0C0, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Fe+Cr+Ni+Mn", elements: [{element: "Fe", count: 9}, {element: "Cr", count: 1}, {element: "Ni", count: 1}, {element: "Mn", count: 1}]},
{name: "electrum",        title_key: "material.electrum",        gen_flags: METAL_FULL, state: SOLID, color: 0xDAC48F, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Au+Ag",    elements: [{element: "Au", count: 1}, {element: "Ag", count: 1}]},
{name: "invar",           title_key: "material.invar",           gen_flags: METAL_FULL, state: SOLID, color: 0xA0A090, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Fe+Ni",    elements: [{element: "Fe", count: 2}, {element: "Ni", count: 1}]},
{name: "cupronickel",     title_key: "material.cupronickel",     gen_flags: METAL_FULL, state: SOLID, color: 0xC0A070, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Cu+Ni",    elements: [{element: "Cu", count: 1}, {element: "Ni", count: 1}]},
{name: "solder",          title_key: "material.solder",          gen_flags: METAL_FULL, state: SOLID, color: 0x909090, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Sn+Pb",    elements: [{element: "Sn", count: 9}, {element: "Pb", count: 1}]},
{name: "tin_alloy",       title_key: "material.tin_alloy",       gen_flags: METAL_FULL, state: SOLID, color: 0xC0C0C0, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Sn+Fe",    elements: [{element: "Sn", count: 1}, {element: "Fe", count: 1}]},
{name: "red_alloy",       title_key: "material.red_alloy",       gen_flags: METAL_FULL, state: SOLID, color: 0xFF4040, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "?",       elements: []},
{name: "annealed_copper", title_key: "material.annealed_copper", gen_flags: METAL_FULL, state: SOLID, color: 0xFFB060, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Cu",      elements: [{element: "Cu", count: 1}]},
{name: "tungstensteel",   title_key: "material.tungstensteel",   gen_flags: METAL_FULL, state: SOLID, color: 0x8080A0, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "W+Fe",     elements: [{element: "W", count: 1}, {element: "Fe", count: 1}]},
{name: "hss_g",           title_key: "material.hss_g",           gen_flags: METAL_FULL, state: SOLID, color: 0xA0A0B0, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "?",       elements: []},
{name: "naquadah",        title_key: "material.naquadah",        gen_flags: METAL_FULL, state: SOLID, color: 0x40A040, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "?",       elements: []},
{name: "naquadah_alloy",  title_key: "material.naquadah_alloy",  gen_flags: METAL_FULL, state: SOLID, color: 0x60C060, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "?",       elements: []},

# More alloys
{name: "magnalium",       title_key: "material.magnalium",       gen_flags: METAL_FULL, state: SOLID, color: 0xC0D0C0, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Mg+Al",  elements: [{element: "Mg", count: 2}, {element: "Al", count: 1}]},
{name: "duralumin",       title_key: "material.duralumin",       gen_flags: METAL_FULL, state: SOLID, color: 0xC0C8D0, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Al+Cu+Mg",  elements: [{element: "Al", count: 93}, {element: "Cu", count: 4}, {element: "Mg", count: 2}]},
{name: "alnico",          title_key: "material.alnico",          gen_flags: METAL_FULL, state: SOLID, color: 0x908090, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Al+Ni+Co+Fe",  elements: [{element: "Al", count: 1}, {element: "Ni", count: 2}, {element: "Co", count: 1}, {element: "Fe", count: 5}]},
{name: "nichrome",        title_key: "material.nichrome",        gen_flags: METAL_FULL, state: SOLID, color: 0xA0A0B0, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Ni+Cr",  elements: [{element: "Ni", count: 4}, {element: "Cr", count: 1}]},
{name: "monel",           title_key: "material.monel",           gen_flags: METAL_FULL, state: SOLID, color: 0x90A090, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Ni+Cu+Fe",  elements: [{element: "Ni", count: 6}, {element: "Cu", count: 3}, {element: "Fe", count: 1}]},
{name: "kovar",           title_key: "material.kovar",           gen_flags: METAL_FULL, state: SOLID, color: 0xA0A0A0, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Fe+Ni+Co",  elements: [{element: "Fe", count: 5}, {element: "Ni", count: 3}, {element: "Co", count: 1}]},
{name: "inconel",         title_key: "material.inconel",         gen_flags: METAL_FULL, state: SOLID, color: 0x9090A0, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Ni+Cr+Fe",  elements: [{element: "Ni", count: 7}, {element: "Cr", count: 2}, {element: "Fe", count: 1}]},
{name: "phosphor_bronze", title_key: "material.phosphor_bronze", gen_flags: METAL_FULL, state: SOLID, color: 0xC08040, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Cu+Sn+P",  elements: [{element: "Cu", count: 10}, {element: "Sn", count: 1}, {element: "P", count: 1}]},
{name: "beryllium_copper", title_key: "material.beryllium_copper", gen_flags: METAL_FULL, state: SOLID, color: 0xD0B080, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Cu+Be",  elements: [{element: "Cu", count: 49}, {element: "Be", count: 1}]},
{name: "titanium_alloy",  title_key: "material.titanium_alloy",  gen_flags: METAL_FULL, state: SOLID, color: 0xA0A0B0, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Ti+Al+V",  elements: [{element: "Ti", count: 44}, {element: "Al", count: 3}, {element: "V", count: 3}]},
{name: "nitinol",         title_key: "material.nitinol",         gen_flags: METAL_FULL, state: SOLID, color: 0x909090, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Ni+Ti",  elements: [{element: "Ni", count: 1}, {element: "Ti", count: 1}]},
{name: "permalloy",       title_key: "material.permalloy",       gen_flags: METAL_FULL, state: SOLID, color: 0xA0A0B0, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Ni+Fe",  elements: [{element: "Ni", count: 4}, {element: "Fe", count: 1}]},
{name: "rose_metal",      title_key: "material.rose_metal",      gen_flags: METAL_FULL, state: SOLID, color: 0xC0A080, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Bi+Pb+Sn",  elements: [{element: "Bi", count: 2}, {element: "Pb", count: 1}, {element: "Sn", count: 1}]},
{name: "woods_metal",     title_key: "material.woods_metal",     gen_flags: METAL_FULL, state: SOLID, color: 0xC0A0A0, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Bi+Pb+Sn+Cd",  elements: [{element: "Bi", count: 2}, {element: "Pb", count: 1}, {element: "Sn", count: 1}, {element: "Cd", count: 1}]},

# Gems
{name: "diamond",        title_key: "material.diamond",        gen_flags: GEM_FULL, state: SOLID, color: 0x80E0E0, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "C",   elements: []},
{name: "ruby",           title_key: "material.ruby",           gen_flags: GEM_FULL, state: SOLID, color: 0xE02020, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Al2O3+Cr", elements: []},
{name: "sapphire",       title_key: "material.sapphire",       gen_flags: GEM_FULL, state: SOLID, color: 0x2040E0, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Al2O3", elements: []},
{name: "emerald",        title_key: "material.emerald",        gen_flags: GEM_FULL, state: SOLID, color: 0x20E020, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Be3Al2Si6O18", elements: []},
{name: "amethyst",       title_key: "material.amethyst",       gen_flags: GEM_FULL, state: SOLID, color: 0x8020E0, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "SiO2", elements: []},
{name: "lapis",          title_key: "material.lapis",          gen_flags: GEM_FULL, state: SOLID, color: 0x2040C0, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Na8Al6Si6O24S2", elements: []},
{name: "quartz",         title_key: "material.quartz",         gen_flags: GEM_FULL, state: SOLID, color: 0xE0E0F0, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "SiO2", elements: []},
{name: "nether_quartz",  title_key: "material.nether_quartz",  gen_flags: GEM_FULL, state: SOLID, color: 0xF0E0E0, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "SiO2", elements: []},

# Rare / exotic metals
{name: "uranium",       title_key: "material.uranium",       gen_flags: ORE_FULL, state: SOLID, color: 0x80B080, melting_point: 1408, boiling_point: 4404, mass: 238.0, chemical_formula: "U",  elements: [{element: "U", count: 1}]},
{name: "plutonium",     title_key: "material.plutonium",     gen_flags: METAL_FULL, state: SOLID, color: 0xB08080, melting_point: 913,  boiling_point: 3501, mass: 244.0, chemical_formula: "Pu", elements: [{element: "Pu", count: 1}]},
{name: "thorium",       title_key: "material.thorium",       gen_flags: ORE_FULL, state: SOLID, color: 0x80A080, melting_point: 2023, boiling_point: 5061, mass: 232.0, chemical_formula: "Th", elements: [{element: "Th", count: 1}]},
{name: "iridium",       title_key: "material.iridium",       gen_flags: ORE_FULL, state: SOLID, color: 0xC0C0D0, melting_point: 2719, boiling_point: 4701, mass: 192.2, chemical_formula: "Ir", elements: [{element: "Ir", count: 1}]},
{name: "osmium",        title_key: "material.osmium",        gen_flags: ORE_FULL, state: SOLID, color: 0x9090A0, melting_point: 3306, boiling_point: 5285, mass: 190.2, chemical_formula: "Os", elements: [{element: "Os", count: 1}]},
{name: "graphene",      title_key: "material.graphene",      gen_flags: DUST_ONLY, state: SOLID, color: 0x202030, melting_point: 0,    boiling_point: 0,   mass: 0,    chemical_formula: "C", elements: []},
{name: "superconductor", title_key: "material.superconductor", gen_flags: METAL_FULL, state: SOLID, color: 0x8040E0, melting_point: 0,    boiling_point: 0,   mass: 0,    chemical_formula: "?", elements: []},

# --- Liquids ---
{name: "water",              title_key: "material.water",              gen_flags: FLUID, state: LIQUID, color: 0x4080FF, melting_point: 273,  boiling_point: 373,  mass: 18.0, chemical_formula: "H2O",   elements: [{element: "H", count: 2}, {element: "O", count: 1}]},
{name: "lava",               title_key: "material.lava",               gen_flags: FLUID, state: LIQUID, color: 0xFF4000, melting_point: 0,    boiling_point: 0,    mass: 0,    chemical_formula: "?",     elements: []},
{name: "steam",              title_key: "material.steam",              gen_flags: GAS,   state: GASEOUS, color: 0xC0C0C0, melting_point: 373,  boiling_point: 373,  mass: 18.0, chemical_formula: "H2O",   elements: [{element: "H", count: 2}, {element: "O", count: 1}]},
{name: "creosote",           title_key: "material.creosote",           gen_flags: FLUID, state: LIQUID, color: 0x5C3A1E, melting_point: 0,    boiling_point: 0,    mass: 0,    chemical_formula: "?",     elements: []},
{name: "sulfuric_acid",      title_key: "material.sulfuric_acid",      gen_flags: FLUID, state: LIQUID, color: 0xC8C800, melting_point: 0,    boiling_point: 0,    mass: 98.1, chemical_formula: "H2SO4", elements: [{element: "H", count: 2}, {element: "S", count: 1}, {element: "O", count: 4}]},
{name: "hydrochloric_acid",  title_key: "material.hydrochloric_acid",  gen_flags: FLUID, state: LIQUID, color: 0xC0FFC0, melting_point: 0,    boiling_point: 0,    mass: 36.5, chemical_formula: "HCl",   elements: [{element: "H", count: 1}, {element: "Cl", count: 1}]},
{name: "nitric_acid",        title_key: "material.nitric_acid",        gen_flags: FLUID, state: LIQUID, color: 0xFFC040, melting_point: 0,    boiling_point: 0,    mass: 63.0, chemical_formula: "HNO3",  elements: [{element: "H", count: 1}, {element: "N", count: 1}, {element: "O", count: 3}]},
{name: "hydrofluoric_acid",  title_key: "material.hydrofluoric_acid",  gen_flags: FLUID, state: LIQUID, color: 0x80FF80, melting_point: 0,    boiling_point: 0,    mass: 20.0, chemical_formula: "HF",    elements: [{element: "H", count: 1}, {element: "F", count: 1}]},
{name: "aqua_regia",         title_key: "material.aqua_regia",         gen_flags: FLUID, state: LIQUID, color: 0xFFA500, melting_point: 0,    boiling_point: 0,    mass: 0,    chemical_formula: "?",     elements: []},
{name: "lubricant",          title_key: "material.lubricant",          gen_flags: FLUID, state: LIQUID, color: 0xE0E080, melting_point: 0,    boiling_point: 0,    mass: 0,    chemical_formula: "?",     elements: []},
{name: "biomass",            title_key: "material.biomass",            gen_flags: FLUID, state: LIQUID, color: 0x406020, melting_point: 0,    boiling_point: 0,    mass: 0,    chemical_formula: "?",     elements: []},
{name: "ethanol",            title_key: "material.ethanol",            gen_flags: FLUID, state: LIQUID, color: 0xE0E0A0, melting_point: 0,    boiling_point: 0,    mass: 46.1, chemical_formula: "C2H5OH", elements: [{element: "C", count: 2}, {element: "H", count: 6}, {element: "O", count: 1}]},
{name: "oil",                title_key: "material.oil",                gen_flags: FLUID, state: LIQUID, color: 0x1C1C1C, melting_point: 0,    boiling_point: 0,    mass: 0,    chemical_formula: "?",     elements: []},
{name: "oil_heavy",          title_key: "material.oil_heavy",          gen_flags: FLUID, state: LIQUID, color: 0x2A1A0A, melting_point: 0,    boiling_point: 0,    mass: 0,    chemical_formula: "?",     elements: []},
{name: "oil_light",          title_key: "material.oil_light",          gen_flags: FLUID, state: LIQUID, color: 0x3A3020, melting_point: 0,    boiling_point: 0,    mass: 0,    chemical_formula: "?",     elements: []},
{name: "fuel_diesel",        title_key: "material.fuel_diesel",        gen_flags: FLUID, state: LIQUID, color: 0x402000, melting_point: 0,    boiling_point: 0,    mass: 0,    chemical_formula: "?",     elements: []},
{name: "fuel_rocket",        title_key: "material.fuel_rocket",        gen_flags: FLUID, state: LIQUID, color: 0x602000, melting_point: 0,    boiling_point: 0,    mass: 0,    chemical_formula: "?",     elements: []},
{name: "glue",               title_key: "material.glue",               gen_flags: FLUID, state: LIQUID, color: 0xD0B060, melting_point: 0,    boiling_point: 0,    mass: 0,    chemical_formula: "?",     elements: []},
{name: "mercury",            title_key: "material.mercury",            gen_flags: FLUID, state: LIQUID, color: 0xE0E0E0, melting_point: 234,  boiling_point: 630,  mass: 200.6, chemical_formula: "Hg",    elements: [{element: "Hg", count: 1}]},
{name: "molten_iron",        title_key: "material.molten_iron",        gen_flags: FLUID, state: LIQUID, color: 0xFF6020, melting_point: 1811, boiling_point: 3134, mass: 55.8,  chemical_formula: "Fe",    elements: [{element: "Fe", count: 1}]},

# --- Gases ---
{name: "oxygen",           title_key: "material.oxygen",           gen_flags: GAS, state: GASEOUS, color: 0x4080FF, melting_point: 54,   boiling_point: 90,   mass: 32.0, chemical_formula: "O2",  elements: [{element: "O", count: 2}]},
{name: "hydrogen",         title_key: "material.hydrogen",         gen_flags: GAS, state: GASEOUS, color: 0x80E0FF, melting_point: 14,   boiling_point: 20,   mass: 2.0,  chemical_formula: "H2",  elements: [{element: "H", count: 2}]},
{name: "nitrogen",         title_key: "material.nitrogen",         gen_flags: GAS, state: GASEOUS, color: 0x80C0FF, melting_point: 63,   boiling_point: 77,   mass: 28.0, chemical_formula: "N2",  elements: [{element: "N", count: 2}]},
{name: "carbon_dioxide",   title_key: "material.carbon_dioxide",   gen_flags: GAS, state: GASEOUS, color: 0xC0C0C0, melting_point: 217,  boiling_point: 195,  mass: 44.0, chemical_formula: "CO2", elements: [{element: "C", count: 1}, {element: "O", count: 2}]},
{name: "carbon_monoxide",  title_key: "material.carbon_monoxide",  gen_flags: GAS, state: GASEOUS, color: 0xA0A0B0, melting_point: 68,   boiling_point: 81,   mass: 28.0, chemical_formula: "CO",  elements: [{element: "C", count: 1}, {element: "O", count: 1}]},
{name: "sulfur_dioxide",   title_key: "material.sulfur_dioxide",   gen_flags: GAS, state: GASEOUS, color: 0xC0E060, melting_point: 200,  boiling_point: 263,  mass: 64.1, chemical_formula: "SO2", elements: [{element: "S", count: 1}, {element: "O", count: 2}]},
{name: "nitrogen_dioxide", title_key: "material.nitrogen_dioxide", gen_flags: GAS, state: GASEOUS, color: 0xE04020, melting_point: 262,  boiling_point: 294,  mass: 46.0, chemical_formula: "NO2", elements: [{element: "N", count: 1}, {element: "O", count: 2}]},
{name: "nitric_oxide",     title_key: "material.nitric_oxide",     gen_flags: GAS, state: GASEOUS, color: 0xA0C0F0, melting_point: 110,  boiling_point: 121,  mass: 30.0, chemical_formula: "NO",  elements: [{element: "N", count: 1}, {element: "O", count: 1}]},
{name: "ammonia",          title_key: "material.ammonia",          gen_flags: GAS, state: GASEOUS, color: 0x80E080, melting_point: 195,  boiling_point: 240,  mass: 17.0, chemical_formula: "NH3", elements: [{element: "N", count: 1}, {element: "H", count: 3}]},
{name: "methane",          title_key: "material.methane",          gen_flags: GAS, state: GASEOUS, color: 0x80C0C0, melting_point: 91,   boiling_point: 112,  mass: 16.0, chemical_formula: "CH4", elements: [{element: "C", count: 1}, {element: "H", count: 4}]},
{name: "natural_gas",      title_key: "material.natural_gas",      gen_flags: GAS, state: GASEOUS, color: 0x90A090, melting_point: 0,    boiling_point: 0,    mass: 0,    chemical_formula: "?",   elements: []},
{name: "hydrogen_sulfide", title_key: "material.hydrogen_sulfide", gen_flags: GAS, state: GASEOUS, color: 0xC0E040, melting_point: 188,  boiling_point: 213,  mass: 34.1, chemical_formula: "H2S", elements: [{element: "H", count: 2}, {element: "S", count: 1}]},
{name: "ozone",            title_key: "material.ozone",            gen_flags: GAS, state: GASEOUS, color: 0x80A0FF, melting_point: 80,   boiling_point: 161,  mass: 48.0, chemical_formula: "O3",  elements: [{element: "O", count: 3}]},
{name: "chlorine",         title_key: "material.chlorine",         gen_flags: GAS, state: GASEOUS, color: 0x80FF80, melting_point: 172,  boiling_point: 239,  mass: 71.0, chemical_formula: "Cl2", elements: [{element: "Cl", count: 2}]},
{name: "fluorine",         title_key: "material.fluorine",         gen_flags: GAS, state: GASEOUS, color: 0x80FFA0, melting_point: 53,   boiling_point: 85,   mass: 38.0, chemical_formula: "F2",  elements: [{element: "F", count: 2}]},
{name: "bromine",          title_key: "material.bromine",          gen_flags: GAS, state: GASEOUS, color: 0xA04020, melting_point: 266,  boiling_point: 332,  mass: 159.8,chemical_formula: "Br2", elements: [{element: "Br", count: 2}]},
{name: "iodine",           title_key: "material.iodine",           gen_flags: GAS, state: GASEOUS, color: 0x6020A0, melting_point: 387,  boiling_point: 458,  mass: 253.8,chemical_formula: "I2",  elements: [{element: "I", count: 2}]},

# Noble gases
{name: "helium",     title_key: "material.helium",     gen_flags: GAS, state: GASEOUS, color: 0xC0E0FF, melting_point: 1,  boiling_point: 4,  mass: 4.0,  chemical_formula: "He", elements: [{element: "He", count: 1}]},
{name: "neon",       title_key: "material.neon",       gen_flags: GAS, state: GASEOUS, color: 0xFF80A0, melting_point: 25, boiling_point: 27, mass: 20.2, chemical_formula: "Ne", elements: [{element: "Ne", count: 1}]},
{name: "argon",      title_key: "material.argon",      gen_flags: GAS, state: GASEOUS, color: 0x80E0FF, melting_point: 84, boiling_point: 87, mass: 39.9, chemical_formula: "Ar", elements: [{element: "Ar", count: 1}]},
{name: "krypton",    title_key: "material.krypton",    gen_flags: GAS, state: GASEOUS, color: 0x80FFE0, melting_point: 116, boiling_point: 120, mass: 83.8, chemical_formula: "Kr", elements: [{element: "Kr", count: 1}]},
{name: "xenon",      title_key: "material.xenon",      gen_flags: GAS, state: GASEOUS, color: 0x8080E0, melting_point: 161, boiling_point: 165, mass: 131.3,chemical_formula: "Xe", elements: [{element: "Xe", count: 1}]},
{name: "radon",      title_key: "material.radon",      gen_flags: GAS, state: GASEOUS, color: 0x2080A0, melting_point: 202, boiling_point: 211, mass: 222.0,chemical_formula: "Rn", elements: [{element: "Rn", count: 1}]},

# Plasmas
{name: "deuterium",       title_key: "material.deuterium",       gen_flags: GAS_PLASMA, state: PLASMA, color: 0x80C0FF, melting_point: 0, boiling_point: 0, mass: 2.0,  chemical_formula: "D2", elements: [{element: "H", count: 2}]},
{name: "tritium",         title_key: "material.tritium",         gen_flags: GAS_PLASMA, state: PLASMA, color: 0x80E0FF, melting_point: 0, boiling_point: 0, mass: 3.0,  chemical_formula: "T2", elements: [{element: "H", count: 2}]},
{name: "helium_3",        title_key: "material.helium_3",        gen_flags: GAS_PLASMA, state: PLASMA, color: 0xC0E0FF, melting_point: 0, boiling_point: 0, mass: 3.0,  chemical_formula: "He3", elements: [{element: "He", count: 1}]},
{name: "plasma_nitrogen", title_key: "material.plasma_nitrogen", gen_flags: FLUID_PLASMA, state: PLASMA, color: 0xA0D0FF, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "N+", elements: [{element: "N", count: 1}]},
{name: "plasma_oxygen",   title_key: "material.plasma_oxygen",   gen_flags: FLUID_PLASMA, state: PLASMA, color: 0x80C0FF, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "O+", elements: [{element: "O", count: 1}]},
{name: "plasma_helium",  title_key: "material.plasma_helium",   gen_flags: FLUID_PLASMA, state: PLASMA, color: 0xC0E0FF, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "He+", elements: [{element: "He", count: 1}]},

# Hydrocarbons / organics
{name: "ethylene",        title_key: "material.ethylene",        gen_flags: GAS, state: GASEOUS, color: 0xA0D0A0, melting_point: 104, boiling_point: 169, mass: 28.1, chemical_formula: "C2H4",  elements: [{element: "C", count: 2}, {element: "H", count: 4}]},
{name: "propylene",       title_key: "material.propylene",       gen_flags: GAS, state: GASEOUS, color: 0x90C0A0, melting_point: 88,  boiling_point: 226, mass: 42.1, chemical_formula: "C3H6",  elements: [{element: "C", count: 3}, {element: "H", count: 6}]},
{name: "benzene",         title_key: "material.benzene",         gen_flags: FLUID, state: LIQUID, color: 0xC0A040, melting_point: 279, boiling_point: 353, mass: 78.1, chemical_formula: "C6H6",  elements: [{element: "C", count: 6}, {element: "H", count: 6}]},
{name: "toluene",         title_key: "material.toluene",         gen_flags: FLUID, state: LIQUID, color: 0xB09040, melting_point: 178, boiling_point: 384, mass: 92.1, chemical_formula: "C7H8",  elements: [{element: "C", count: 7}, {element: "H", count: 8}]},
{name: "phenol",          title_key: "material.phenol",          gen_flags: FLUID, state: LIQUID, color: 0xD0A060, melting_point: 314, boiling_point: 455, mass: 94.1, chemical_formula: "C6H5OH", elements: [{element: "C", count: 6}, {element: "H", count: 6}, {element: "O", count: 1}]},
{name: "formaldehyde",    title_key: "material.formaldehyde",    gen_flags: GAS, state: GASEOUS, color: 0xC0C0A0, melting_point: 181, boiling_point: 254, mass: 30.0, chemical_formula: "CH2O",  elements: [{element: "C", count: 1}, {element: "H", count: 2}, {element: "O", count: 1}]},
{name: "acetic_acid",     title_key: "material.acetic_acid",     gen_flags: FLUID, state: LIQUID, color: 0xE0D0A0, melting_point: 290, boiling_point: 391, mass: 60.1, chemical_formula: "C2H4O2", elements: [{element: "C", count: 2}, {element: "H", count: 4}, {element: "O", count: 2}]},
{name: "acetone",         title_key: "material.acetone",         gen_flags: FLUID, state: LIQUID, color: 0xE0E0B0, melting_point: 178, boiling_point: 329, mass: 58.1, chemical_formula: "C3H6O",  elements: [{element: "C", count: 3}, {element: "H", count: 6}, {element: "O", count: 1}]},
{name: "glycerol",        title_key: "material.glycerol",        gen_flags: FLUID, state: LIQUID, color: 0xE0D0B0, melting_point: 291, boiling_point: 563, mass: 92.1, chemical_formula: "C3H8O3", elements: [{element: "C", count: 3}, {element: "H", count: 8}, {element: "O", count: 3}]},
{name: "vinyl_chloride",  title_key: "material.vinyl_chloride",  gen_flags: GAS, state: GASEOUS, color: 0xA0B0A0, melting_point: 140, boiling_point: 260, mass: 62.5, chemical_formula: "C2H3Cl", elements: [{element: "C", count: 2}, {element: "H", count: 3}, {element: "Cl", count: 1}]},
{name: "styrene",         title_key: "material.styrene",         gen_flags: FLUID, state: LIQUID, color: 0xC0A060, melting_point: 242, boiling_point: 418, mass: 104.2,chemical_formula: "C8H8",  elements: [{element: "C", count: 8}, {element: "H", count: 8}]},

# More organics
{name: "methanol",        title_key: "material.methanol",        gen_flags: FLUID, state: LIQUID, color: 0xC0D0C0, melting_point: 176, boiling_point: 338, mass: 32.0, chemical_formula: "CH3OH", elements: [{element: "C", count: 1}, {element: "H", count: 4}, {element: "O", count: 1}]},
{name: "ethane",          title_key: "material.ethane",          gen_flags: GAS, state: GASEOUS, color: 0x90B0B0, melting_point: 90,  boiling_point: 185, mass: 30.1, chemical_formula: "C2H6",  elements: [{element: "C", count: 2}, {element: "H", count: 6}]},
{name: "propane",         title_key: "material.propane",         gen_flags: GAS, state: GASEOUS, color: 0x90B0A0, melting_point: 86,  boiling_point: 231, mass: 44.1, chemical_formula: "C3H8",  elements: [{element: "C", count: 3}, {element: "H", count: 8}]},
{name: "butane",          title_key: "material.butane",          gen_flags: GAS, state: GASEOUS, color: 0x90B0A0, melting_point: 135, boiling_point: 273, mass: 58.1, chemical_formula: "C4H10", elements: [{element: "C", count: 4}, {element: "H", count: 10}]},
{name: "butadiene",       title_key: "material.butadiene",       gen_flags: GAS, state: GASEOUS, color: 0xA0C0A0, melting_point: 164, boiling_point: 269, mass: 54.1, chemical_formula: "C4H6",  elements: [{element: "C", count: 4}, {element: "H", count: 6}]},
{name: "isoprene",        title_key: "material.isoprene",        gen_flags: FLUID, state: LIQUID, color: 0xC0A080, melting_point: 127, boiling_point: 307, mass: 68.1, chemical_formula: "C5H8",  elements: [{element: "C", count: 5}, {element: "H", count: 8}]},
{name: "naphthalene",     title_key: "material.naphthalene",     gen_flags: DUST_ONLY, state: SOLID, color: 0xD0D0E0, melting_point: 353, boiling_point: 491, mass: 128.2,chemical_formula: "C10H8", elements: [{element: "C", count: 10}, {element: "H", count: 8}]},
{name: "aniline",         title_key: "material.aniline",         gen_flags: FLUID, state: LIQUID, color: 0xC0A060, melting_point: 267, boiling_point: 457, mass: 93.1, chemical_formula: "C6H5NH2", elements: [{element: "C", count: 6}, {element: "H", count: 7}, {element: "N", count: 1}]},
{name: "chloroform",      title_key: "material.chloroform",      gen_flags: FLUID, state: LIQUID, color: 0xC0D0C0, melting_point: 210, boiling_point: 334, mass: 119.4,chemical_formula: "CHCl3", elements: [{element: "C", count: 1}, {element: "H", count: 1}, {element: "Cl", count: 3}]},
{name: "carbon_tetrachloride", title_key: "material.carbon_tetrachloride", gen_flags: FLUID, state: LIQUID, color: 0xE0E0E0, melting_point: 250, boiling_point: 350, mass: 153.8,chemical_formula: "CCl4",  elements: [{element: "C", count: 1}, {element: "Cl", count: 4}]},
{name: "ethylene_glycol", title_key: "material.ethylene_glycol", gen_flags: FLUID, state: LIQUID, color: 0xD0E0D0, melting_point: 260, boiling_point: 470, mass: 62.1, chemical_formula: "C2H6O2", elements: [{element: "C", count: 2}, {element: "H", count: 6}, {element: "O", count: 2}]},
{name: "dimethyl_ether",  title_key: "material.dimethyl_ether",  gen_flags: GAS, state: GASEOUS, color: 0xA0C0C0, melting_point: 132, boiling_point: 248, mass: 46.1, chemical_formula: "C2H6O",  elements: [{element: "C", count: 2}, {element: "H", count: 6}, {element: "O", count: 1}]},

# Wood (special: DUST + PLATE + ROD + BLOCK)
{name: "wood", title_key: "material.wood", gen_flags: DUST_ONLY | GEN_METAL | GEN_BLOCK, state: SOLID, color: 0x8B5E3C, melting_point: 0, boiling_point: 0, mass: 0.5, chemical_formula: "C6H10O5", elements: []},

# Planetary rock types
{name: "granite",     title_key: "material.granite",     gen_flags: DUST_ONLY, state: SOLID, color: 0xA09890, melting_point: 0, boiling_point: 0, mass: 2.7, chemical_formula: "SiO2+Al2O3",   elements: []},
{name: "basalt",      title_key: "material.basalt",      gen_flags: DUST_ONLY, state: SOLID, color: 0x505050, melting_point: 0, boiling_point: 0, mass: 3.0, chemical_formula: "SiO2+FeO",     elements: []},
{name: "marble",      title_key: "material.marble",      gen_flags: DUST_ONLY, state: SOLID, color: 0xE8E0D8, melting_point: 0, boiling_point: 0, mass: 2.7, chemical_formula: "CaCO3",       elements: []},
{name: "sandstone",   title_key: "material.sandstone",   gen_flags: DUST_ONLY, state: SOLID, color: 0xC8A870, melting_point: 0, boiling_point: 0, mass: 2.3, chemical_formula: "SiO2",        elements: []},
{name: "shale",       title_key: "material.shale",       gen_flags: DUST_ONLY, state: SOLID, color: 0x5A6050, melting_point: 0, boiling_point: 0, mass: 2.5, chemical_formula: "SiO2+Al2O3",  elements: []},
{name: "komatiite",   title_key: "material.komatiite",   gen_flags: DUST_ONLY, state: SOLID, color: 0x3A5030, melting_point: 0, boiling_point: 0, mass: 3.2, chemical_formula: "MgO+SiO2",    elements: []},
{name: "regolith",    title_key: "material.regolith",    gen_flags: DUST_ONLY, state: SOLID, color: 0xA06040, melting_point: 0, boiling_point: 0, mass: 1.8, chemical_formula: "SiO2+Fe2O3",  elements: []},
{name: "anorthosite", title_key: "material.anorthosite", gen_flags: DUST_ONLY, state: SOLID, color: 0xC0C0C8, melting_point: 0, boiling_point: 0, mass: 2.7, chemical_formula: "CaAl2Si2O8", elements: []},

# Pure element solids (periodic table completeness)
{name: "lithium",    title_key: "material.lithium",    gen_flags: ORE_FULL, state: SOLID, color: 0xB0B0B0, melting_point: 454,  boiling_point: 1615, mass: 6.9,   chemical_formula: "Li", elements: [{element: "Li", count: 1}]},
{name: "beryllium",  title_key: "material.beryllium",  gen_flags: ORE_FULL, state: SOLID, color: 0xA0C0A0, melting_point: 1560, boiling_point: 3243, mass: 9.0,   chemical_formula: "Be", elements: [{element: "Be", count: 1}]},
{name: "boron",      title_key: "material.boron",      gen_flags: DUST_ONLY, state: SOLID, color: 0x808080, melting_point: 2349, boiling_point: 4200, mass: 10.8,  chemical_formula: "B",  elements: [{element: "B", count: 1}]},
{name: "sodium",     title_key: "material.sodium",     gen_flags: ORE_FULL, state: SOLID, color: 0xC0C0C0, melting_point: 371,  boiling_point: 1156, mass: 23.0,  chemical_formula: "Na", elements: [{element: "Na", count: 1}]},
{name: "magnesium",  title_key: "material.magnesium",  gen_flags: ORE_FULL, state: SOLID, color: 0xD0D0D0, melting_point: 923,  boiling_point: 1363, mass: 24.3,  chemical_formula: "Mg", elements: [{element: "Mg", count: 1}]},
{name: "silicon",    title_key: "material.silicon",    gen_flags: ORE_FULL, state: SOLID, color: 0x808090, melting_point: 1687, boiling_point: 3173, mass: 28.1,  chemical_formula: "Si", elements: [{element: "Si", count: 1}]},
{name: "phosphorus", title_key: "material.phosphorus", gen_flags: DUST_ONLY, state: SOLID, color: 0xC06040, melting_point: 317,  boiling_point: 554,  mass: 31.0,  chemical_formula: "P",  elements: [{element: "P", count: 1}]},
{name: "sulfur",     title_key: "material.sulfur",     gen_flags: DUST_ONLY, state: SOLID, color: 0xC0C040, melting_point: 388,  boiling_point: 718,  mass: 32.1,  chemical_formula: "S",  elements: [{element: "S", count: 1}]},
{name: "potassium",  title_key: "material.potassium",  gen_flags: ORE_FULL, state: SOLID, color: 0xB0A080, melting_point: 337,  boiling_point: 1032, mass: 39.1,  chemical_formula: "K",  elements: [{element: "K", count: 1}]},
{name: "calcium",    title_key: "material.calcium",    gen_flags: ORE_FULL, state: SOLID, color: 0xC0C0A0, melting_point: 1115, boiling_point: 1757, mass: 40.1,  chemical_formula: "Ca", elements: [{element: "Ca", count: 1}]},
{name: "scandium",   title_key: "material.scandium",   gen_flags: ORE_FULL, state: SOLID, color: 0xA0A0B0, melting_point: 1814, boiling_point: 3103, mass: 45.0,  chemical_formula: "Sc", elements: [{element: "Sc", count: 1}]},
{name: "vanadium",   title_key: "material.vanadium",   gen_flags: ORE_FULL, state: SOLID, color: 0xA0A0C0, melting_point: 2183, boiling_point: 3680, mass: 50.9,  chemical_formula: "V",  elements: [{element: "V", count: 1}]},
{name: "gallium",    title_key: "material.gallium",    gen_flags: ORE_FULL, state: SOLID, color: 0xC0C0C0, melting_point: 303,  boiling_point: 2477, mass: 69.7,  chemical_formula: "Ga", elements: [{element: "Ga", count: 1}]},
{name: "germanium",  title_key: "material.germanium",  gen_flags: ORE_FULL, state: SOLID, color: 0xA0A0A0, melting_point: 1211, boiling_point: 3106, mass: 72.6,  chemical_formula: "Ge", elements: [{element: "Ge", count: 1}]},
{name: "arsenic",    title_key: "material.arsenic",    gen_flags: DUST_ONLY, state: SOLID, color: 0x808080, melting_point: 1090, boiling_point: 887,  mass: 74.9,  chemical_formula: "As", elements: [{element: "As", count: 1}]},
{name: "selenium",   title_key: "material.selenium",   gen_flags: DUST_ONLY, state: SOLID, color: 0xC0A040, melting_point: 494,  boiling_point: 958,  mass: 79.0,  chemical_formula: "Se", elements: [{element: "Se", count: 1}]},
{name: "rubidium",   title_key: "material.rubidium",   gen_flags: ORE_FULL, state: SOLID, color: 0xA09080, melting_point: 312,  boiling_point: 961,  mass: 85.5,  chemical_formula: "Rb", elements: [{element: "Rb", count: 1}]},
{name: "strontium",  title_key: "material.strontium",  gen_flags: ORE_FULL, state: SOLID, color: 0xC0B0A0, melting_point: 1050, boiling_point: 1655, mass: 87.6,  chemical_formula: "Sr", elements: [{element: "Sr", count: 1}]},
{name: "yttrium",    title_key: "material.yttrium",    gen_flags: ORE_FULL, state: SOLID, color: 0xA0A0A0, melting_point: 1799, boiling_point: 3609, mass: 88.9,  chemical_formula: "Y",  elements: [{element: "Y", count: 1}]},
{name: "zirconium",  title_key: "material.zirconium",  gen_flags: ORE_FULL, state: SOLID, color: 0xA0B0B0, melting_point: 2128, boiling_point: 4650, mass: 91.2,  chemical_formula: "Zr", elements: [{element: "Zr", count: 1}]},
{name: "niobium",    title_key: "material.niobium",    gen_flags: ORE_FULL, state: SOLID, color: 0xA0A0B0, melting_point: 2750, boiling_point: 5017, mass: 92.9,  chemical_formula: "Nb", elements: [{element: "Nb", count: 1}]},
{name: "molybdenum", title_key: "material.molybdenum", gen_flags: ORE_FULL, state: SOLID, color: 0xA0A0A0, melting_point: 2896, boiling_point: 4912, mass: 95.9,  chemical_formula: "Mo", elements: [{element: "Mo", count: 1}]},
{name: "ruthenium",  title_key: "material.ruthenium",  gen_flags: ORE_FULL, state: SOLID, color: 0xA0A0A0, melting_point: 2607, boiling_point: 4423, mass: 101.1, chemical_formula: "Ru", elements: [{element: "Ru", count: 1}]},
{name: "rhodium",    title_key: "material.rhodium",    gen_flags: ORE_FULL, state: SOLID, color: 0xC0C0D0, melting_point: 2237, boiling_point: 3968, mass: 102.9, chemical_formula: "Rh", elements: [{element: "Rh", count: 1}]},
{name: "palladium",  title_key: "material.palladium",  gen_flags: ORE_FULL, state: SOLID, color: 0xC0C0C0, melting_point: 1828, boiling_point: 3236, mass: 106.4, chemical_formula: "Pd", elements: [{element: "Pd", count: 1}]},
{name: "cadmium",    title_key: "material.cadmium",    gen_flags: ORE_FULL, state: SOLID, color: 0xA0A0A0, melting_point: 594,  boiling_point: 1040, mass: 112.4, chemical_formula: "Cd", elements: [{element: "Cd", count: 1}]},
{name: "indium",     title_key: "material.indium",     gen_flags: ORE_FULL, state: SOLID, color: 0xB0B0C0, melting_point: 430,  boiling_point: 2345, mass: 114.8, chemical_formula: "In", elements: [{element: "In", count: 1}]},
{name: "tellurium",  title_key: "material.tellurium",  gen_flags: DUST_ONLY, state: SOLID, color: 0xB0A060, melting_point: 723,  boiling_point: 1261, mass: 127.6, chemical_formula: "Te", elements: [{element: "Te", count: 1}]},
{name: "cesium",     title_key: "material.cesium",     gen_flags: ORE_FULL, state: SOLID, color: 0xA09070, melting_point: 302,  boiling_point: 944,  mass: 132.9, chemical_formula: "Cs", elements: [{element: "Cs", count: 1}]},
{name: "barium",     title_key: "material.barium",     gen_flags: ORE_FULL, state: SOLID, color: 0xB0B0A0, melting_point: 1000, boiling_point: 2070, mass: 137.3, chemical_formula: "Ba", elements: [{element: "Ba", count: 1}]},
{name: "lanthanum",  title_key: "material.lanthanum",  gen_flags: ORE_FULL, state: SOLID, color: 0xA0A0A0, melting_point: 1193, boiling_point: 3737, mass: 138.9, chemical_formula: "La", elements: [{element: "La", count: 1}]},
{name: "hafnium",    title_key: "material.hafnium",    gen_flags: ORE_FULL, state: SOLID, color: 0xA0A0A0, melting_point: 2506, boiling_point: 4876, mass: 178.5, chemical_formula: "Hf", elements: [{element: "Hf", count: 1}]},
{name: "tantalum",   title_key: "material.tantalum",   gen_flags: ORE_FULL, state: SOLID, color: 0xA0A0A0, melting_point: 3290, boiling_point: 5731, mass: 180.9, chemical_formula: "Ta", elements: [{element: "Ta", count: 1}]},
{name: "rhenium",    title_key: "material.rhenium",    gen_flags: ORE_FULL, state: SOLID, color: 0xA0A0A0, melting_point: 3459, boiling_point: 5869, mass: 186.2, chemical_formula: "Re", elements: [{element: "Re", count: 1}]},
{name: "thallium",   title_key: "material.thallium",   gen_flags: ORE_FULL, state: SOLID, color: 0xA0A0A0, melting_point: 577,  boiling_point: 1746, mass: 204.4, chemical_formula: "Tl", elements: [{element: "Tl", count: 1}]},
{name: "polonium",   title_key: "material.polonium",   gen_flags: DUST_ONLY, state: SOLID, color: 0x808080, melting_point: 527,  boiling_point: 1235, mass: 209.0, chemical_formula: "Po", elements: [{element: "Po", count: 1}]},

# --- Intermediate oxides (synthesis/processing intermediates) ---
{name: "alumina",          title_key: "material.alumina",          gen_flags: DUST_ONLY, state: SOLID, color: 0xFFFFFF, melting_point: 2345, boiling_point: 3250, mass: 102.0, chemical_formula: "Al2O3",      elements: [{element: "Al", count: 2}, {element: "O", count: 3}]},
{name: "magnesia",         title_key: "material.magnesia",         gen_flags: DUST_ONLY, state: SOLID, color: 0xF0F0F0, melting_point: 3125, boiling_point: 3870, mass: 40.3,  chemical_formula: "MgO",        elements: [{element: "Mg", count: 1}, {element: "O", count: 1}]},
{name: "quicklime",        title_key: "material.quicklime",        gen_flags: DUST_ONLY, state: SOLID, color: 0xF8F8F0, melting_point: 2845, boiling_point: 3850, mass: 56.1,  chemical_formula: "CaO",        elements: [{element: "Ca", count: 1}, {element: "O", count: 1}]},
{name: "zinc_oxide",       title_key: "material.zinc_oxide",       gen_flags: DUST_ONLY, state: SOLID, color: 0xF0F0E0, melting_point: 2248, boiling_point: 2630, mass: 81.4,  chemical_formula: "ZnO",        elements: [{element: "Zn", count: 1}, {element: "O", count: 1}]},
{name: "cupric_oxide",     title_key: "material.cupric_oxide",     gen_flags: DUST_ONLY, state: SOLID, color: 0x202020, melting_point: 1599, boiling_point: 2270, mass: 79.5,  chemical_formula: "CuO",        elements: [{element: "Cu", count: 1}, {element: "O", count: 1}]},
{name: "cuprous_oxide",    title_key: "material.cuprous_oxide",    gen_flags: DUST_ONLY, state: SOLID, color: 0xC02020, melting_point: 1508, boiling_point: 2070, mass: 143.1, chemical_formula: "Cu2O",       elements: [{element: "Cu", count: 2}, {element: "O", count: 1}]},
{name: "nickel_oxide",     title_key: "material.nickel_oxide",     gen_flags: DUST_ONLY, state: SOLID, color: 0x709070, melting_point: 2228, boiling_point: 2370, mass: 74.7,  chemical_formula: "NiO",        elements: [{element: "Ni", count: 1}, {element: "O", count: 1}]},
{name: "litharge",         title_key: "material.litharge",         gen_flags: DUST_ONLY, state: SOLID, color: 0xC06020, melting_point: 1161, boiling_point: 1745, mass: 223.2, chemical_formula: "PbO",        elements: [{element: "Pb", count: 1}, {element: "O", count: 1}]},
{name: "lead_dioxide",     title_key: "material.lead_dioxide",     gen_flags: DUST_ONLY, state: SOLID, color: 0x604020, melting_point: 563,  boiling_point: 0,    mass: 239.2, chemical_formula: "PbO2",       elements: [{element: "Pb", count: 1}, {element: "O", count: 2}]},
{name: "chromium_oxide",   title_key: "material.chromium_oxide",   gen_flags: DUST_ONLY, state: SOLID, color: 0x408040, melting_point: 2708, boiling_point: 3270, mass: 152.0, chemical_formula: "Cr2O3",      elements: [{element: "Cr", count: 2}, {element: "O", count: 3}]},
{name: "manganous_oxide",  title_key: "material.manganous_oxide",  gen_flags: DUST_ONLY, state: SOLID, color: 0x408040, melting_point: 1945, boiling_point: 3570, mass: 70.9,  chemical_formula: "MnO",        elements: [{element: "Mn", count: 1}, {element: "O", count: 1}]},
{name: "manganese_dioxide", title_key: "material.manganese_dioxide", gen_flags: DUST_ONLY, state: SOLID, color: 0x202020, melting_point: 808,  boiling_point: 0,    mass: 86.9,  chemical_formula: "MnO2",       elements: [{element: "Mn", count: 1}, {element: "O", count: 2}]},
{name: "cobalt_oxide",     title_key: "material.cobalt_oxide",     gen_flags: DUST_ONLY, state: SOLID, color: 0x202020, melting_point: 2203, boiling_point: 2370, mass: 74.9,  chemical_formula: "CoO",        elements: [{element: "Co", count: 1}, {element: "O", count: 1}]},
{name: "silver_oxide",     title_key: "material.silver_oxide",     gen_flags: DUST_ONLY, state: SOLID, color: 0x806040, melting_point: 553,  boiling_point: 0,    mass: 231.7, chemical_formula: "Ag2O",       elements: [{element: "Ag", count: 2}, {element: "O", count: 1}]},
{name: "tungsten_trioxide", title_key: "material.tungsten_trioxide", gen_flags: DUST_ONLY, state: SOLID, color: 0xC0C060, melting_point: 1746, boiling_point: 2270, mass: 231.8, chemical_formula: "WO3",        elements: [{element: "W", count: 1}, {element: "O", count: 3}]},
{name: "molybdenum_trioxide", title_key: "material.molybdenum_trioxide", gen_flags: DUST_ONLY, state: SOLID, color: 0xE0E0F0, melting_point: 1068, boiling_point: 1428, mass: 144.0, chemical_formula: "MoO3",    elements: [{element: "Mo", count: 1}, {element: "O", count: 3}]},
{name: "vanadium_pentoxide", title_key: "material.vanadium_pentoxide", gen_flags: DUST_ONLY, state: SOLID, color: 0xE08020, melting_point: 943,  boiling_point: 2270, mass: 181.9, chemical_formula: "V2O5",     elements: [{element: "V", count: 2}, {element: "O", count: 5}]},
{name: "niobium_pentoxide", title_key: "material.niobium_pentoxide", gen_flags: DUST_ONLY, state: SOLID, color: 0xF0E8F0, melting_point: 1785, boiling_point: 3270, mass: 265.8, chemical_formula: "Nb2O5",    elements: [{element: "Nb", count: 2}, {element: "O", count: 5}]},
{name: "tantalum_pentoxide", title_key: "material.tantalum_pentoxide", gen_flags: DUST_ONLY, state: SOLID, color: 0xF0F0F0, melting_point: 2072, boiling_point: 3270, mass: 441.9, chemical_formula: "Ta2O5",    elements: [{element: "Ta", count: 2}, {element: "O", count: 5}]},
{name: "antimony_trioxide", title_key: "material.antimony_trioxide", gen_flags: DUST_ONLY, state: SOLID, color: 0xF0F0F0, melting_point: 929,  boiling_point: 1725, mass: 291.5, chemical_formula: "Sb2O3",    elements: [{element: "Sb", count: 2}, {element: "O", count: 3}]},
{name: "bismuth_trioxide", title_key: "material.bismuth_trioxide", gen_flags: DUST_ONLY, state: SOLID, color: 0xE0E060, melting_point: 1090, boiling_point: 2170, mass: 466.0, chemical_formula: "Bi2O3",    elements: [{element: "Bi", count: 2}, {element: "O", count: 3}]},
{name: "beryllia",         title_key: "material.beryllia",         gen_flags: DUST_ONLY, state: SOLID, color: 0xF0F0F0, melting_point: 2851, boiling_point: 4170, mass: 25.0,  chemical_formula: "BeO",        elements: [{element: "Be", count: 1}, {element: "O", count: 1}]},
{name: "boron_trioxide",   title_key: "material.boron_trioxide",   gen_flags: DUST_ONLY, state: SOLID, color: 0xE8E8E8, melting_point: 723,  boiling_point: 2320, mass: 69.6,  chemical_formula: "B2O3",       elements: [{element: "B", count: 2}, {element: "O", count: 3}]},
{name: "phosphorus_pentoxide", title_key: "material.phosphorus_pentoxide", gen_flags: DUST_ONLY, state: SOLID, color: 0xF0F0F0, melting_point: 613,  boiling_point: 633,  mass: 142.0, chemical_formula: "P2O5",  elements: [{element: "P", count: 2}, {element: "O", count: 5}]},
{name: "sulfur_trioxide",  title_key: "material.sulfur_trioxide",  gen_flags: FLUID, state: LIQUID, color: 0xE0E0E0, melting_point: 290,  boiling_point: 318,  mass: 80.1,  chemical_formula: "SO3",        elements: [{element: "S", count: 1}, {element: "O", count: 3}]},
{name: "dinitrogen_monoxide", title_key: "material.dinitrogen_monoxide", gen_flags: GAS, state: GASEOUS, color: 0xC0D0F0, melting_point: 182,  boiling_point: 185,  mass: 44.0,  chemical_formula: "N2O",     elements: [{element: "N", count: 2}, {element: "O", count: 1}]},
{name: "chlorine_dioxide", title_key: "material.chlorine_dioxide",  gen_flags: GAS, state: GASEOUS, color: 0xF0E040, melting_point: 214,  boiling_point: 284,  mass: 67.5,  chemical_formula: "ClO2",      elements: [{element: "Cl", count: 1}, {element: "O", count: 2}]},
{name: "iron_ii_oxide",    title_key: "material.iron_ii_oxide",    gen_flags: DUST_ONLY, state: SOLID, color: 0x202020, melting_point: 1650, boiling_point: 2773, mass: 71.8,  chemical_formula: "FeO",        elements: [{element: "Fe", count: 1}, {element: "O", count: 1}]},
{name: "iron_iii_oxide",   title_key: "material.iron_iii_oxide",   gen_flags: DUST_ONLY, state: SOLID, color: 0xC04020, melting_point: 1838, boiling_point: 1273, mass: 159.7, chemical_formula: "Fe2O3",      elements: [{element: "Fe", count: 2}, {element: "O", count: 3}]},
{name: "uranium_dioxide",  title_key: "material.uranium_dioxide",  gen_flags: DUST_ONLY, state: SOLID, color: 0x402020, melting_point: 3140, boiling_point: 3770, mass: 270.0, chemical_formula: "UO2",        elements: [{element: "U", count: 1}, {element: "O", count: 2}]},
{name: "thorium_dioxide",  title_key: "material.thorium_dioxide",  gen_flags: DUST_ONLY, state: SOLID, color: 0xE8E0E0, melting_point: 3660, boiling_point: 4400, mass: 264.0, chemical_formula: "ThO2",       elements: [{element: "Th", count: 1}, {element: "O", count: 2}]},
{name: "zirconia",         title_key: "material.zirconia",         gen_flags: DUST_ONLY, state: SOLID, color: 0xF0F0F0, melting_point: 2988, boiling_point: 5273, mass: 123.2, chemical_formula: "ZrO2",       elements: [{element: "Zr", count: 1}, {element: "O", count: 2}]},
{name: "tin_dioxide",      title_key: "material.tin_dioxide",      gen_flags: DUST_ONLY, state: SOLID, color: 0xF0F0F0, melting_point: 1903, boiling_point: 3270, mass: 150.7, chemical_formula: "SnO2",       elements: [{element: "Sn", count: 1}, {element: "O", count: 2}]},
{name: "titania",          title_key: "material.titania",          gen_flags: DUST_ONLY, state: SOLID, color: 0xF0F0F0, melting_point: 2128, boiling_point: 3245, mass: 79.9,  chemical_formula: "TiO2",       elements: [{element: "Ti", count: 1}, {element: "O", count: 2}]},
{name: "chromium_trioxide", title_key: "material.chromium_trioxide", gen_flags: DUST_ONLY, state: SOLID, color: 0xC02020, melting_point: 470,  boiling_point: 523,  mass: 100.0, chemical_formula: "CrO3",    elements: [{element: "Cr", count: 1}, {element: "O", count: 3}]},
{name: "osmium_tetroxide", title_key: "material.osmium_tetroxide", gen_flags: DUST_ONLY, state: SOLID, color: 0xE0E8A0, melting_point: 313,  boiling_point: 403,  mass: 254.2, chemical_formula: "OsO4",      elements: [{element: "Os", count: 1}, {element: "O", count: 4}]},
{name: "silicon_dioxide",  title_key: "material.silicon_dioxide",  gen_flags: DUST_ONLY, state: SOLID, color: 0xE0E0E0, melting_point: 1986, boiling_point: 3223, mass: 60.1,  chemical_formula: "SiO2",       elements: [{element: "Si", count: 1}, {element: "O", count: 2}]},

# --- Hydroxides ---
{name: "sodium_hydroxide",     title_key: "material.sodium_hydroxide",     gen_flags: DUST_ONLY, state: SOLID, color: 0xF0F0F0, melting_point: 591,  boiling_point: 1661, mass: 40.0,  chemical_formula: "NaOH",      elements: [{element: "Na", count: 1}, {element: "O", count: 1}, {element: "H", count: 1}]},
{name: "potassium_hydroxide",  title_key: "material.potassium_hydroxide",  gen_flags: DUST_ONLY, state: SOLID, color: 0xF0F0F0, melting_point: 679,  boiling_point: 1608, mass: 56.1,  chemical_formula: "KOH",       elements: [{element: "K", count: 1}, {element: "O", count: 1}, {element: "H", count: 1}]},
{name: "calcium_hydroxide",    title_key: "material.calcium_hydroxide",    gen_flags: DUST_ONLY, state: SOLID, color: 0xF0F0E0, melting_point: 839,  boiling_point: 0,    mass: 74.1,  chemical_formula: "Ca(OH)2",   elements: [{element: "Ca", count: 1}, {element: "O", count: 2}, {element: "H", count: 2}]},
{name: "magnesium_hydroxide",  title_key: "material.magnesium_hydroxide",  gen_flags: DUST_ONLY, state: SOLID, color: 0xF0F0F0, melting_point: 623,  boiling_point: 0,    mass: 58.3,  chemical_formula: "Mg(OH)2",   elements: [{element: "Mg", count: 1}, {element: "O", count: 2}, {element: "H", count: 2}]},
{name: "aluminum_hydroxide",   title_key: "material.aluminum_hydroxide",   gen_flags: DUST_ONLY, state: SOLID, color: 0xF0F0F0, melting_point: 573,  boiling_point: 0,    mass: 78.0,  chemical_formula: "Al(OH)3",   elements: [{element: "Al", count: 1}, {element: "O", count: 3}, {element: "H", count: 3}]},
{name: "iron_ii_hydroxide",    title_key: "material.iron_ii_hydroxide",    gen_flags: DUST_ONLY, state: SOLID, color: 0x408040, melting_point: 0,    boiling_point: 0,    mass: 89.9,  chemical_formula: "Fe(OH)2",   elements: [{element: "Fe", count: 1}, {element: "O", count: 2}, {element: "H", count: 2}]},
{name: "iron_iii_hydroxide",   title_key: "material.iron_iii_hydroxide",   gen_flags: DUST_ONLY, state: SOLID, color: 0x804020, melting_point: 0,    boiling_point: 0,    mass: 106.9, chemical_formula: "Fe(OH)3",   elements: [{element: "Fe", count: 1}, {element: "O", count: 3}, {element: "H", count: 3}]},

# --- Common salts ---
{name: "soda_ash",          title_key: "material.soda_ash",          gen_flags: DUST_ONLY, state: SOLID, color: 0xF0F0F0, melting_point: 1124, boiling_point: 1870, mass: 106.0, chemical_formula: "Na2CO3",    elements: [{element: "Na", count: 2}, {element: "C", count: 1}, {element: "O", count: 3}]},
{name: "sodium_bicarbonate", title_key: "material.sodium_bicarbonate", gen_flags: DUST_ONLY, state: SOLID, color: 0xE8E8E8, melting_point: 543,  boiling_point: 0,    mass: 84.0,  chemical_formula: "NaHCO3",    elements: [{element: "Na", count: 1}, {element: "H", count: 1}, {element: "C", count: 1}, {element: "O", count: 3}]},
{name: "saltpeter",         title_key: "material.saltpeter",         gen_flags: DUST_ONLY, state: SOLID, color: 0xF0F0F0, melting_point: 607,  boiling_point: 0,    mass: 101.1, chemical_formula: "KNO3",      elements: [{element: "K", count: 1}, {element: "N", count: 1}, {element: "O", count: 3}]},
{name: "sodium_nitrate",    title_key: "material.sodium_nitrate",    gen_flags: DUST_ONLY, state: SOLID, color: 0xF0F0F0, melting_point: 581,  boiling_point: 0,    mass: 85.0,  chemical_formula: "NaNO3",     elements: [{element: "Na", count: 1}, {element: "N", count: 1}, {element: "O", count: 3}]},
{name: "ammonium_nitrate",  title_key: "material.ammonium_nitrate",  gen_flags: DUST_ONLY, state: SOLID, color: 0xF0F0F0, melting_point: 442,  boiling_point: 483,  mass: 80.0,  chemical_formula: "NH4NO3",    elements: [{element: "N", count: 2}, {element: "H", count: 4}, {element: "O", count: 3}]},
{name: "ammonium_chloride", title_key: "material.ammonium_chloride", gen_flags: DUST_ONLY, state: SOLID, color: 0xF0F0F0, melting_point: 611,  boiling_point: 793,  mass: 53.5,  chemical_formula: "NH4Cl",     elements: [{element: "N", count: 1}, {element: "H", count: 4}, {element: "Cl", count: 1}]},
{name: "calcium_sulfate",   title_key: "material.calcium_sulfate",   gen_flags: DUST_ONLY, state: SOLID, color: 0xF0F0F0, melting_point: 1723, boiling_point: 0,    mass: 136.1, chemical_formula: "CaSO4",     elements: [{element: "Ca", count: 1}, {element: "S", count: 1}, {element: "O", count: 4}]},
{name: "magnesium_sulfate", title_key: "material.magnesium_sulfate", gen_flags: DUST_ONLY, state: SOLID, color: 0xF0F0F0, melting_point: 1385, boiling_point: 0,    mass: 120.4, chemical_formula: "MgSO4",     elements: [{element: "Mg", count: 1}, {element: "S", count: 1}, {element: "O", count: 4}]},
{name: "sodium_sulfate",    title_key: "material.sodium_sulfate",    gen_flags: DUST_ONLY, state: SOLID, color: 0xF0F0F0, melting_point: 1157, boiling_point: 1700, mass: 142.0, chemical_formula: "Na2SO4",    elements: [{element: "Na", count: 2}, {element: "S", count: 1}, {element: "O", count: 4}]},
{name: "copper_sulfate",    title_key: "material.copper_sulfate",    gen_flags: DUST_ONLY, state: SOLID, color: 0x4040C0, melting_point: 773,  boiling_point: 0,    mass: 159.6, chemical_formula: "CuSO4",     elements: [{element: "Cu", count: 1}, {element: "S", count: 1}, {element: "O", count: 4}]},
{name: "iron_ii_sulfate",   title_key: "material.iron_ii_sulfate",   gen_flags: DUST_ONLY, state: SOLID, color: 0x60B060, melting_point: 673,  boiling_point: 0,    mass: 151.9, chemical_formula: "FeSO4",     elements: [{element: "Fe", count: 1}, {element: "S", count: 1}, {element: "O", count: 4}]},
{name: "iron_iii_sulfate",  title_key: "material.iron_iii_sulfate",  gen_flags: DUST_ONLY, state: SOLID, color: 0xC0C060, melting_point: 753,  boiling_point: 0,    mass: 399.9, chemical_formula: "Fe2(SO4)3", elements: [{element: "Fe", count: 2}, {element: "S", count: 3}, {element: "O", count: 12}]},
{name: "zinc_sulfate",      title_key: "material.zinc_sulfate",      gen_flags: DUST_ONLY, state: SOLID, color: 0xF0F0F0, melting_point: 773,  boiling_point: 0,    mass: 161.5, chemical_formula: "ZnSO4",     elements: [{element: "Zn", count: 1}, {element: "S", count: 1}, {element: "O", count: 4}]},
{name: "nickel_sulfate",    title_key: "material.nickel_sulfate",    gen_flags: DUST_ONLY, state: SOLID, color: 0x80D080, melting_point: 1053, boiling_point: 0,    mass: 154.8, chemical_formula: "NiSO4",     elements: [{element: "Ni", count: 1}, {element: "S", count: 1}, {element: "O", count: 4}]},
{name: "cobalt_sulfate",    title_key: "material.cobalt_sulfate",    gen_flags: DUST_ONLY, state: SOLID, color: 0xC04060, melting_point: 1073, boiling_point: 0,    mass: 155.0, chemical_formula: "CoSO4",     elements: [{element: "Co", count: 1}, {element: "S", count: 1}, {element: "O", count: 4}]},
{name: "manganese_sulfate", title_key: "material.manganese_sulfate", gen_flags: DUST_ONLY, state: SOLID, color: 0xD0A0B0, melting_point: 973,  boiling_point: 0,    mass: 151.0, chemical_formula: "MnSO4",     elements: [{element: "Mn", count: 1}, {element: "S", count: 1}, {element: "O", count: 4}]},
{name: "aluminum_sulfate",  title_key: "material.aluminum_sulfate",  gen_flags: DUST_ONLY, state: SOLID, color: 0xF0F0F0, melting_point: 1043, boiling_point: 0,    mass: 342.2, chemical_formula: "Al2(SO4)3", elements: [{element: "Al", count: 2}, {element: "S", count: 3}, {element: "O", count: 12}]},
{name: "sodium_chloride",   title_key: "material.sodium_chloride",   gen_flags: DUST_ONLY, state: SOLID, color: 0xF0F0F0, melting_point: 1074, boiling_point: 1738, mass: 58.4,  chemical_formula: "NaCl",      elements: [{element: "Na", count: 1}, {element: "Cl", count: 1}]},
{name: "potassium_chloride", title_key: "material.potassium_chloride", gen_flags: DUST_ONLY, state: SOLID, color: 0xF0F0F0, melting_point: 1043, boiling_point: 1690, mass: 74.6,  chemical_formula: "KCl",     elements: [{element: "K", count: 1}, {element: "Cl", count: 1}]},
{name: "calcium_carbonate", title_key: "material.calcium_carbonate", gen_flags: DUST_ONLY, state: SOLID, color: 0xF8F8F0, melting_point: 1612, boiling_point: 0,    mass: 100.1, chemical_formula: "CaCO3",     elements: [{element: "Ca", count: 1}, {element: "C", count: 1}, {element: "O", count: 3}]},

# --- Other mixtures / industrial materials ---
{name: "glass",              title_key: "material.glass",              gen_flags: DUST_ONLY, state: SOLID, color: 0xE0E8F0, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "SiO2+Na2O+CaO",   elements: []},
{name: "borosilicate_glass", title_key: "material.borosilicate_glass", gen_flags: DUST_ONLY, state: SOLID, color: 0xE0E8F0, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "SiO2+B2O3",      elements: []},
{name: "concrete",           title_key: "material.concrete",           gen_flags: DUST_ONLY, state: SOLID, color: 0x909090, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "CaO+SiO2",       elements: []},
{name: "refractory_cement",  title_key: "material.refractory_cement",  gen_flags: DUST_ONLY, state: SOLID, color: 0xA09890, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Al2O3+SiO2",     elements: []},
{name: "porcelain",          title_key: "material.porcelain",          gen_flags: DUST_ONLY, state: SOLID, color: 0xF0E8E0, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "Al2O3+SiO2+K2O", elements: []},
{name: "slag",               title_key: "material.slag",               gen_flags: DUST_ONLY, state: SOLID, color: 0x404040, melting_point: 0, boiling_point: 0, mass: 0, chemical_formula: "CaO+SiO2+Al2O3",  elements: []},
]

# Mineral compound item keys registered as mod items (not C++ materials).
# Each entry: [item_key, title_key_override_or_empty]
# The register_compounds() function auto-generates title_key from item_key.
# Order does not matter; grouped by category for readability.
const _ALL_COMPOUNDS := [
	# --- GT-style mineral compounds (already referenced by terrain blocks) ---
	["crushed.magnetite"],      # Fe3O4
	["crushed.pyrite"],         # FeS2
	["crushed.bauxite"],        # Al2O3·xH2O
	["crushed.cassiterite"],    # SnO2
	["crushed.ilmenite"],       # FeTiO3
	["crushed.pentlandite"],    # (Fe,Ni)9S8
	["crushed.chalcopyrite"],   # CuFeS2
	["crushed.sphalerite"],     # ZnS
	["crushed.galena"],         # PbS
	["dust.cinnabar"],          # HgS (terrain uses dust form)
	["dust.fluorite"],          # CaF2 (terrain uses dust form)
	["dust.graphite"],          # C   (terrain uses dust form)
	["dust.salt"],              # NaCl (terrain uses dust form)

	# --- Additional common ore compounds (changed from pure-element drops) ---
	["crushed.hematite"],       # Fe2O3  → replaces MAT_ORE_IRON drop
	["crushed.malachite"],      # Cu2CO3(OH)2 → replaces MAT_ORE_COPPER drop
	["crushed.pyrolusite"],     # MnO2  → replaces MAT_ORE_MANGANESE drop
	["crushed.wolframite"],     # (Fe,Mn)WO4 → replaces MAT_ORE_TUNGSTEN drop
	["crushed.rutile"],         # TiO2  → replaces MAT_ORE_TITANIUM drop
	["crushed.sperrylite"],     # PtAs2 → replaces MAT_ORE_PLATINUM drop
	["crushed.cobaltite"],      # CoAsS → replaces MAT_ORE_COBALT drop
	["crushed.uraninite"],      # UO2   → replaces MAT_ORE_URANIUM drop

	# --- Periodic table element ore minerals ---
	# Pure elements (IDs 121-156) now drop their realistic ore compound.
	["crushed.spodumene"],      # LiAlSi2O6
	["crushed.bertrandite"],    # Be4Si2O7(OH)2
	["crushed.kernite"],        # Na2B4O6(OH)2·3H2O
	["crushed.magnesite"],      # MgCO3
	["crushed.silica"],         # SiO2
	["crushed.apatite"],        # Ca5(PO4)3F
	["crushed.sylvite"],        # KCl
	["crushed.calcite"],        # CaCO3
	["crushed.thortveitite"],   # Sc2Si2O7
	["crushed.patronite"],      # VS4
	["crushed.chromite"],       # FeCr2O4
	["crushed.gallite"],        # CuGaS2
	["crushed.germanite"],      # Cu26Fe4Ge4S32
	["crushed.arsenopyrite"],   # FeAsS
	["crushed.clausthalite"],   # PbSe
	["crushed.bromargyrite"],   # AgBr
	["crushed.celestine"],      # SrSO4
	["crushed.xenotime"],       # YPO4
	["crushed.zircon"],         # ZrSiO4
	["crushed.columbite"],      # FeNb2O6
	["crushed.molybdenite"],    # MoS2
	["crushed.laurite"],        # RuS2
	["crushed.stibiopalladinite"], # Pd5Sb2
	["crushed.rhodium_oxide"],  # Rh2O3 → replaces MAT_ORE_RHODIUM drop
	["crushed.greenockite"],    # CdS
	["crushed.roquesite"],      # CuInS2
	["crushed.stibnite"],       # Sb2S3
	["crushed.calaverite"],     # AuTe2
	["crushed.iodargyrite"],    # AgI
	["crushed.pollucite"],      # Cs(AlSi2O6)
	["crushed.barite"],         # BaSO4
	["crushed.monazite"],       # (Ce,La)PO4
	["crushed.baddeleyite"],    # HfO2
	["crushed.tantalite"],      # FeTa2O6
	["crushed.rheniite"],       # ReS2
	["crushed.osmiridium"],     # Os-Ir native alloy (PGM)
	["crushed.iridium_oxide"],  # IrO2
	["crushed.thorite"],        # ThSiO4 → replaces MAT_ORE_THORIUM drop
	["crushed.lepidolite"],     # K(Li,Al,Rb)3(Al,Si)4O10(F,OH)2 → Rb source
	["crushed.bismuthinite"],   # Bi2S3 → replaces MAT_ORE_BISMUTH drop
	["crushed.crookesite"],     # Cu7TlSe4

	# Additional processing intermediates / byproducts
	["crushed.phosphorite"],    # Ca3(PO4)2 → phosphorus source
	["dust.ash"],               # General burning byproduct
	["dust.sawdust"],           # Wood processing byproduct
]

# Convenience constants for GDScript code that references material IDs.
const MATERIAL_STONE      = 0
const MATERIAL_COAL       = 2
const MATERIAL_COPPER     = 5
const MATERIAL_IRON       = 7
const MATERIAL_WOOD       = 112
const MATERIAL_GRANITE    = 113
const MATERIAL_BASALT     = 114
const MATERIAL_MARBLE     = 115
const MATERIAL_SANDSTONE  = 116
const MATERIAL_SHALE      = 117
const MATERIAL_KOMATIITE  = 118
const MATERIAL_REGOLITH   = 119
const MATERIAL_ANORTHOSTIE = 120
# New element constants (IDs 121-156) are available via C++ materials:: namespace
# or can be added here as needed.
