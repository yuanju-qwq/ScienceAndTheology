extends Node

class WikiEntry extends RefCounted:
	var id: String
	var category: String
	var icon: Texture2D
	var title: String
	var subtitle: String
	var description: String
	var properties: Array[Dictionary]  # [{label, value}, ...]
	var related_ids: PackedStringArray


enum Category {
	MATERIALS = 0,
	FLUIDS = 1,
	ITEMS = 2,
	MAGIC = 3,
	GUIDES = 4,
}

const CATEGORY_ORDER: Array[String] = ["materials", "fluids", "items", "magic", "guides"]
const CATEGORY_LABELS: Dictionary = {
	materials = "Materials",
	fluids = "Fluids",
	items = "Items",
	magic = "Magic",
	guides = "Guides",
}

## Materials sub-categories
const MAT_SUB: Dictionary = {
	primitive = "Stone & Primitive",
	metals = "Basic Metals",
	alloys = "Alloys",
	gems = "Gems",
	rare = "Rare Metals",
	liquids = "Liquids",
	gases = "Gases",
	noble = "Noble Gases",
	plasma = "Plasma-grade",
	organic = "Organics",
}

# All registered entries keyed by id.
var entries: Dictionary = {}  # String -> WikiEntry
# Entries in display order per category.
var _category_order: Dictionary = {}  # String -> PackedStringArray


# ── State labels ──────────────────────────────────────────────────────────
const _STATE_SOLID  = "Solid"
const _STATE_LIQUID = "Liquid"
const _STATE_GAS    = "Gas"
const _STATE_PLASMA = "Plasma"


# ── Lifecycle ─────────────────────────────────────────────────────────────
func _ready() -> void:
	_register_materials()
	_register_fluids()
	_register_items()
	_register_magic()
	_register_guides()


# ── Public API ────────────────────────────────────────────────────────────
func get_entry(id: String) -> WikiEntry:
	return entries.get(id, null)


func get_entries_in_category(category: String) -> Array[WikiEntry]:
	var result: Array[WikiEntry] = []
	var order: PackedStringArray = _category_order.get(category, PackedStringArray())
	for eid in order:
		var e: WikiEntry = entries.get(eid)
		if e:
			result.append(e)
	return result


func search(query: String) -> Array[WikiEntry]:
	var result: Array[WikiEntry] = []
	var q := query.to_lower()
	for e: WikiEntry in entries.values():
		if q.is_empty() or e.title.to_lower().contains(q):
			result.append(e)
	return result


func make_placeholder_icon(color: Color, size: int = 20) -> ImageTexture:
	var image := Image.create(size, size, false, Image.FORMAT_RGBA8)
	var border_c := Color(color.r * 0.6, color.g * 0.6, color.b * 0.6, 1.0)
	for y in size:
		for x in size:
			if x == 0 or y == 0 or x == size - 1 or y == size - 1:
				image.set_pixel(x, y, border_c)
			elif (int(x / 4.0) + int(y / 4.0)) % 2 == 0:
				image.set_pixel(x, y, color)
			else:
				image.set_pixel(x, y, color.darkened(0.12))
	var tex := ImageTexture.create_from_image(image)
	return tex


# ── Registration helpers ──────────────────────────────────────────────────
func _reg(id: String, category: String, color: Color, title: String,
		  subtitle: String, description: String, properties: Array = [],
		  related: PackedStringArray = PackedStringArray()) -> WikiEntry:
	var e := WikiEntry.new()
	e.id = id
	e.category = category
	e.icon = make_placeholder_icon(color)
	e.title = title
	e.subtitle = subtitle
	e.description = description
	e.properties.assign(properties)
	e.related_ids = related
	entries[id] = e

	if not _category_order.has(category):
		_category_order[category] = PackedStringArray()
	var arr: PackedStringArray = _category_order[category]
	_category_order[category] = arr + PackedStringArray([id])
	return e


func _k2c(k: float) -> float:
	return k - 273.15


func _temp_str(kelvin: int, show_c: bool = true) -> String:
	if kelvin <= 0:
		return "—"
	if show_c:
		return "%d K  (%d°C)" % [kelvin, int(_k2c(kelvin))]
	return "%d K" % kelvin


func _mass_str(mass: float) -> String:
	if mass >= 100.0:
		return "%.0f" % mass
	if mass >= 10.0:
		return "%.1f" % mass
	return "%.2f" % mass


func _formula_display(formula: String) -> String:
	if formula == "?" or formula.is_empty():
		return "Unknown"
	return formula


# ── Material registration (113 entries) ───────────────────────────────────
func _register_materials() -> void:
	# Each entry: [id_suffix, display_name, color_hex, state_str, melt_K, boil_K, mass, formula, subcategory]
	var mat_data: Array = [
		# ── Primitive / Stone-age ──
		["stone",    "Stone",            0x808080, _STATE_SOLID,     0,    0, 1.0,    "SiO2",   "primitive"],
		["flint",    "Flint",            0x303030, _STATE_SOLID,     0,    0, 1.0,    "SiO2",   "primitive"],
		["coal",     "Coal",             0x1A1A1A, _STATE_SOLID,     0,    0, 1.0,    "C",      "primitive"],
		["charcoal", "Charcoal",         0x2A1A0A, _STATE_SOLID,     0,    0, 1.0,    "C",      "primitive"],
		["lignite",  "Lignite",          0x3A2A1A, _STATE_SOLID,     0,    0, 1.0,    "C",      "primitive"],
		# ── Basic Metals ──
		["copper",     "Copper",         0xFF7F24, _STATE_SOLID,  1358, 2835, 63.5,  "Cu",     "metals"],
		["tin",        "Tin",            0xD3D3D3, _STATE_SOLID,   505, 2875, 118.7, "Sn",     "metals"],
		["iron",       "Iron",           0xBEBEBE, _STATE_SOLID,  1811, 3134, 55.8,  "Fe",     "metals"],
		["lead",       "Lead",           0x4A3B5C, _STATE_SOLID,   601, 2022, 207.2, "Pb",     "metals"],
		["silver",     "Silver",         0xE0FFFF, _STATE_SOLID,  1235, 2435, 107.9, "Ag",     "metals"],
		["gold",       "Gold",           0xFFD700, _STATE_SOLID,  1337, 3129, 197.0, "Au",     "metals"],
		["zinc",       "Zinc",           0xFAFAD2, _STATE_SOLID,   693, 1180, 65.4,  "Zn",     "metals"],
		["nickel",     "Nickel",         0xC0C0D0, _STATE_SOLID,  1728, 3186, 58.7,  "Ni",     "metals"],
		["aluminium",  "Aluminium",      0xE8C396, _STATE_SOLID,   933, 2743, 27.0,  "Al",     "metals"],
		["platinum",   "Platinum",       0xE5E4E2, _STATE_SOLID,  2041, 4098, 195.1, "Pt",     "metals"],
		["tungsten",   "Tungsten",       0x78828B, _STATE_SOLID,  3695, 5828, 183.8, "W",      "metals"],
		["titanium",   "Titanium",       0xC4B0C0, _STATE_SOLID,  1941, 3560, 47.9,  "Ti",     "metals"],
		["chrome",     "Chrome",         0xE5B2C9, _STATE_SOLID,  2180, 2944, 52.0,  "Cr",     "metals"],
		["manganese",  "Manganese",      0xD0C0C0, _STATE_SOLID,  1519, 2334, 54.9,  "Mn",     "metals"],
		["cobalt",     "Cobalt",         0x0047AB, _STATE_SOLID,  1768, 3200, 58.9,  "Co",     "metals"],
		["bismuth",    "Bismuth",        0x7CB7BB, _STATE_SOLID,   545, 1837, 209.0, "Bi",     "metals"],
		["antimony",   "Antimony",       0xE0E0F0, _STATE_SOLID,   904, 1860, 121.8, "Sb",     "metals"],
		# ── Alloys ──
		["bronze",           "Bronze",            0xCD7F32, _STATE_SOLID, 1183,    0, 80.2,  "Cu3Sn",      "alloys"],
		["brass",            "Brass",             0xB5A642, _STATE_SOLID, 1193,    0, 69.3,  "Cu3Zn",      "alloys"],
		["steel",            "Steel",             0x808080, _STATE_SOLID, 1811,    0, 51.9,  "FeC",        "alloys"],
		["stainless_steel",  "Stainless Steel",   0xC8C8DC, _STATE_SOLID, 1700,    0, 55.4,  "Fe9CrNiMn",  "alloys"],
		["electrum",         "Electrum",          0xFFFF66, _STATE_SOLID, 1283,    0, 152.5, "AuAg",       "alloys"],
		["invar",            "Invar",             0xC0C096, _STATE_SOLID, 1700,    0, 57.3,  "Fe2Ni",      "alloys"],
		["cupronickel",      "Cupronickel",       0xD7B740, _STATE_SOLID, 1600,    0, 58.3,  "CuNi",       "alloys"],
		["solder",           "Solder",            0xA0A0C0, _STATE_SOLID,  450,    0, 140.0, "Sn9Pb",      "alloys"],
		["tin_alloy",        "Tin Alloy",         0xD0E0E0, _STATE_SOLID,  600,    0, 65.0,  "SnFe",       "alloys"],
		["red_alloy",        "Red Alloy",         0xCC3300, _STATE_SOLID,  600,    0, 50.0,  "Cu?",        "alloys"],
		["annealed_copper",  "Annealed Copper",   0xFF8C42, _STATE_SOLID, 1000,    0, 63.5,  "Cu",         "alloys"],
		["tungstensteel",    "Tungstensteel",     0x7070A0, _STATE_SOLID, 3200,    0, 119.8, "WFe",        "alloys"],
		["hss_g",            "HSS-G",             0x808090, _STATE_SOLID, 2500,    0, 55.0,  "?",          "alloys"],
		["naquadah",         "Naquadah",          0x004400, _STATE_SOLID, 5400,    0, 300.0, "Nq",         "alloys"],
		["naquadah_alloy",   "Naquadah Alloy",    0x006600, _STATE_SOLID, 7200,    0, 310.0, "NqAl",       "alloys"],
		# ── Gems ──
		["diamond",        "Diamond",         0xCCFFFF, _STATE_SOLID, 0, 0, 12.0,   "C",               "gems"],
		["ruby",           "Ruby",            0xFF0000, _STATE_SOLID, 0, 0, 102.0,  "Al2O3",           "gems"],
		["sapphire",       "Sapphire",        0x0000FF, _STATE_SOLID, 0, 0, 102.0,  "Al2O3",           "gems"],
		["emerald",        "Emerald",         0x00FF00, _STATE_SOLID, 0, 0, 175.0,  "Be3Al2Si6O18",    "gems"],
		["amethyst",       "Amethyst",        0xCC66FF, _STATE_SOLID, 0, 0, 60.0,   "SiO2",            "gems"],
		["lapis",          "Lapis",           0x0000AA, _STATE_SOLID, 0, 0, 200.0,  "(Na,Ca)Al6Si6O24S4", "gems"],
		["quartz",         "Quartz",          0xE0E0E0, _STATE_SOLID, 0, 0, 60.0,   "SiO2",            "gems"],
		["nether_quartz",  "Nether Quartz",   0xFFAAAA, _STATE_SOLID, 0, 0, 60.0,   "SiO2",            "gems"],
		# ── Rare / Exotic Metals ──
		["uranium",        "Uranium",         0x3C8C3C, _STATE_SOLID, 1405, 4404, 238.0, "U",       "rare"],
		["plutonium",      "Plutonium",       0x8C003C, _STATE_SOLID,  913, 3503, 244.0, "Pu",      "rare"],
		["thorium",        "Thorium",         0x2C2C2C, _STATE_SOLID, 2115, 5061, 232.0, "Th",      "rare"],
		["iridium",        "Iridium",         0xD0D0FF, _STATE_SOLID, 2719, 4701, 192.2, "Ir",      "rare"],
		["osmium",         "Osmium",          0x5B7D9C, _STATE_SOLID, 3306, 5285, 190.2, "Os",      "rare"],
		["graphene",       "Graphene",        0x404040, _STATE_SOLID,    0,    0, 12.0,  "C",       "rare"],
		["superconductor", "Superconductor",  0xFFFFFF, _STATE_SOLID,   92,    0, 100.0, "?",       "rare"],
		# ── Liquids ──
		["water",             "Water",              0x3355FF, _STATE_LIQUID,  273,  373, 18.0,   "H2O",        "liquids"],
		["lava",              "Lava",               0xFF4400, _STATE_LIQUID, 1500,    0, 100.0,  "?",          "liquids"],
		["creosote",          "Creosote",           0x806040, _STATE_LIQUID,  400,  533, 100.0,  "?",          "liquids"],
		["sulfuric_acid",     "Sulfuric Acid",      0xCCAA00, _STATE_LIQUID,  283,  610, 98.0,   "H2SO4",      "liquids"],
		["hydrochloric_acid", "Hydrochloric Acid",  0x99CC99, _STATE_LIQUID,  188,  383, 36.5,   "HCl",        "liquids"],
		["nitric_acid",       "Nitric Acid",        0xCCCC00, _STATE_LIQUID,  231,  356, 63.0,   "HNO3",       "liquids"],
		["hydrofluoric_acid", "Hydrofluoric Acid",  0x88CC88, _STATE_LIQUID,  190,  393, 20.0,   "HF",         "liquids"],
		["aqua_regia",        "Aqua Regia",         0xFFAA00, _STATE_LIQUID,  231,    0, 63.0,   "HNO3+3HCl",  "liquids"],
		["lubricant",         "Lubricant",          0xCCBB55, _STATE_LIQUID,  300,  600, 400.0,  "?",          "liquids"],
		["biomass",           "Biomass",            0x338833, _STATE_LIQUID,  300,  500, 100.0,  "?",          "liquids"],
		["ethanol",           "Ethanol",            0xDDBB88, _STATE_LIQUID,  159,  351, 46.0,   "C2H5OH",     "liquids"],
		["oil",               "Oil",                0x2A2A2A, _STATE_LIQUID,  300,  600, 200.0,  "?",          "liquids"],
		["oil_heavy",         "Heavy Oil",          0x1A1A1A, _STATE_LIQUID,  350,  700, 300.0,  "?",          "liquids"],
		["oil_light",         "Light Oil",          0x3A2A1A, _STATE_LIQUID,  250,  500, 150.0,  "?",          "liquids"],
		["fuel_diesel",       "Diesel Fuel",        0xCCA000, _STATE_LIQUID,  250,  450, 180.0,  "?",          "liquids"],
		["fuel_rocket",       "Rocket Fuel",        0xFF6600, _STATE_LIQUID,  200,  400, 120.0,  "?",          "liquids"],
		["glue",              "Glue",               0xDDDD88, _STATE_LIQUID,  300,  500, 150.0,  "?",          "liquids"],
		["mercury",           "Mercury",            0xD0D0D0, _STATE_LIQUID,  234,  630, 200.6,  "Hg",         "liquids"],
		["molten_iron",       "Molten Iron",        0xFF6600, _STATE_LIQUID, 1811,    0, 55.8,   "Fe",         "liquids"],
		["steam",             "Steam",              0xC0C0C0, _STATE_GAS,     373,  373, 18.0,   "H2O",        "liquids"],
		# ── Gases ──
		["oxygen",           "Oxygen",            0x80C0FF, _STATE_GAS,  54,  90, 32.0,  "O2",     "gases"],
		["hydrogen",         "Hydrogen",          0x80C0FF, _STATE_GAS,  14,  20,  2.0,  "H2",     "gases"],
		["nitrogen",         "Nitrogen",          0x80FFC0, _STATE_GAS,  63,  77, 28.0,  "N2",     "gases"],
		["carbon_dioxide",   "Carbon Dioxide",    0xA0A0A0, _STATE_GAS,   0, 195, 44.0,  "CO2",    "gases"],
		["carbon_monoxide",  "Carbon Monoxide",   0xB0B0B0, _STATE_GAS,  68,  82, 28.0,  "CO",     "gases"],
		["sulfur_dioxide",   "Sulfur Dioxide",    0xCCCC88, _STATE_GAS, 200, 263, 64.0,  "SO2",    "gases"],
		["nitrogen_dioxide", "Nitrogen Dioxide",  0xCC6600, _STATE_GAS, 262, 294, 46.0,  "NO2",    "gases"],
		["nitric_oxide",     "Nitric Oxide",      0xAADDFF, _STATE_GAS, 109, 121, 30.0,  "NO",     "gases"],
		["ammonia",          "Ammonia",           0x88CCFF, _STATE_GAS, 195, 240, 17.0,  "NH3",    "gases"],
		["methane",          "Methane",           0x8866CC, _STATE_GAS,  91, 112, 16.0,  "CH4",    "gases"],
		["natural_gas",      "Natural Gas",       0x8877BB, _STATE_GAS,  90, 111, 18.0,  "CH4+",   "gases"],
		["hydrogen_sulfide", "Hydrogen Sulfide",  0xCCCC00, _STATE_GAS, 187, 213, 34.0,  "H2S",    "gases"],
		["ozone",            "Ozone",             0x4488FF, _STATE_GAS,  80, 161, 48.0,  "O3",     "gases"],
		["chlorine",         "Chlorine",          0x88FF88, _STATE_GAS, 172, 239, 70.9,  "Cl2",    "gases"],
		["fluorine",         "Fluorine",          0xCCFFCC, _STATE_GAS,  54,  85, 38.0,  "F2",     "gases"],
		["bromine",          "Bromine",           0x884422, _STATE_GAS, 266, 332, 159.8, "Br2",    "gases"],
		["iodine",           "Iodine",            0x6644AA, _STATE_GAS, 387, 458, 253.8, "I2",     "gases"],
		# ── Noble Gases ──
		["helium",  "Helium",  0xFFFFCC, _STATE_GAS,   1,   4,  4.0,  "He",  "noble"],
		["neon",    "Neon",    0xFF4444, _STATE_GAS,  25,  27, 20.2,  "Ne",  "noble"],
		["argon",   "Argon",   0x44FF44, _STATE_GAS,  84,  87, 39.9,  "Ar",  "noble"],
		["krypton", "Krypton", 0x4444FF, _STATE_GAS, 116, 120, 83.8,  "Kr",  "noble"],
		["xenon",   "Xenon",   0x8844FF, _STATE_GAS, 161, 165, 131.3, "Xe",  "noble"],
		["radon",   "Radon",   0xFF44FF, _STATE_GAS, 202, 211, 222.0, "Rn",  "noble"],
		# ── Plasma-grade ──
		["deuterium",       "Deuterium",     0xAAAAFF, _STATE_GAS,     19, 24,  4.0,  "D2",   "plasma"],
		["tritium",         "Tritium",       0x8888FF, _STATE_GAS,     20, 25,  6.0,  "T2",   "plasma"],
		["helium_3",        "Helium-3",      0xFFFFDD, _STATE_GAS,      1,  4,  3.0,  "He-3", "plasma"],
		["plasma_nitrogen", "Plasma N2",     0xAAFFCC, _STATE_PLASMA,    0,  0, 28.0,  "N2*",  "plasma"],
		["plasma_oxygen",   "Plasma O2",     0xAACCFF, _STATE_PLASMA,    0,  0, 32.0,  "O2*",  "plasma"],
		["plasma_helium",   "Plasma He",     0xFFFFEE, _STATE_PLASMA,    0,  0,  4.0,  "He*",  "plasma"],
		# ── Organics ──
		["ethylene",       "Ethylene",        0xCCBB88, _STATE_GAS,     104, 169, 28.0,  "C2H4",     "organic"],
		["propylene",      "Propylene",       0xDDBB88, _STATE_GAS,      88, 225, 42.0,  "C3H6",     "organic"],
		["benzene",        "Benzene",         0xCCBB66, _STATE_LIQUID,  279, 353, 78.0,  "C6H6",     "organic"],
		["toluene",        "Toluene",         0xCCAA55, _STATE_LIQUID,  178, 384, 92.0,  "C7H8",     "organic"],
		["phenol",         "Phenol",          0xDDBB99, _STATE_LIQUID,  314, 455, 94.0,  "C6H5OH",   "organic"],
		["formaldehyde",   "Formaldehyde",    0xCCBBA0, _STATE_GAS,     181, 254, 30.0,  "CH2O",     "organic"],
		["acetic_acid",    "Acetic Acid",     0xCCBB66, _STATE_LIQUID,  290, 391, 60.0,  "C2H4O2",   "organic"],
		["acetone",        "Acetone",         0xAAAACC, _STATE_LIQUID,  178, 329, 58.0,  "C3H6O",    "organic"],
		["glycerol",       "Glycerol",        0xCCCC88, _STATE_LIQUID,  291, 563, 92.0,  "C3H8O3",   "organic"],
		["vinyl_chloride", "Vinyl Chloride",  0xCCBBCC, _STATE_GAS,     119, 260, 62.5,  "C2H3Cl",   "organic"],
		["styrene",        "Styrene",         0xDDBB99, _STATE_LIQUID,  243, 418, 104.0, "C8H8",     "organic"],
		["wood",           "Wood",            0x8B5E3C, _STATE_SOLID,     0,   0,  0.5,  "C6H10O5",  "organic"],
	]

	for d: Array in mat_data:
		var suffix: String = d[0]
		var entry_name: String = d[1]
		var col: int = d[2]
		var state: String = d[3]
		var melt: int = d[4]
		var boil: int = d[5]
		var mass: float = d[6]
		var formula: String = d[7]
		var subcat: String = d[8]

		var eid := "mat." + suffix
		var color := Color.hex(col)

		var subtype := ""
		if MAT_SUB.has(subcat):
			subtype = MAT_SUB[subcat]
		var subtitle := state
		if not subtype.is_empty():
			subtitle = subtype + " · " + state

		# Build description.
		var lines: Array[String] = [entry_name + " (" + _formula_display(formula) + ")."]
		lines.append("State: " + state + ".")

		if melt > 0:
			lines.append(("Melting point: " + _temp_str(melt) + "."))
		if boil > 0:
			lines.append(("Boiling point: " + _temp_str(boil) + "."))
		lines.append(("Relative mass: " + _mass_str(mass) + "."))

		var props: Array[Dictionary] = [
			{"label": "State", "value": state},
			{"label": "Formula", "value": _formula_display(formula)},
			{"label": "Mass", "value": _mass_str(mass)},
		]
		if melt > 0:
			props.append({"label": "Melting Point", "value": _temp_str(melt)})
		if boil > 0:
			props.append({"label": "Boiling Point", "value": _temp_str(boil)})

		_reg(eid, "materials", color, entry_name, subtitle, "\n".join(lines), props)


# ── Fluid registration (29 entries) ──────────────────────────────────────
func _register_fluids() -> void:
	var fluid_data: Array = [
		["water",             "Water",               0x3355FF, 300, false, "H2O"],
		["lava",              "Lava",                0xFF4400, 1500, false, "?"],
		["steam",             "Steam",               0xC0C0C0, 400, true, "H2O"],
		["oil",               "Oil",                 0x2A2A2A, 300, false, "?"],
		["oil_heavy",         "Heavy Oil",           0x1A1A1A, 350, false, "?"],
		["oil_light",         "Light Oil",           0x3A2A1A, 250, false, "?"],
		["fuel_diesel",       "Diesel Fuel",         0xCCA000, 250, false, "?"],
		["fuel_rocket",       "Rocket Fuel",         0xFF6600, 200, false, "?"],
		["ethanol",           "Ethanol",             0xDDBB88, 300, false, "C2H5OH"],
		["sulfuric_acid",     "Sulfuric Acid",       0xCCAA00, 300, false, "H2SO4"],
		["hydrochloric_acid", "Hydrochloric Acid",   0x99CC99, 300, false, "HCl"],
		["nitric_acid",       "Nitric Acid",         0xCCCC00, 300, false, "HNO3"],
		["hydrofluoric_acid", "Hydrofluoric Acid",   0x88CC88, 300, false, "HF"],
		["aqua_regia",        "Aqua Regia",          0xFFAA00, 300, false, "HNO3+3HCl"],
		["creosote",          "Creosote",            0x806040, 400, false, "?"],
		["lubricant",         "Lubricant",           0xCCBB55, 300, false, "?"],
		["glue",              "Glue",                0xDDDD88, 300, false, "?"],
		["biomass",           "Biomass",             0x338833, 300, false, "?"],
		["oxygen",            "Oxygen",              0x80C0FF, 90, true, "O2"],
		["hydrogen",          "Hydrogen",            0x80C0FF, 20, true, "H2"],
		["nitrogen",          "Nitrogen",            0x80FFC0, 77, true, "N2"],
		["natural_gas",       "Natural Gas",         0x8877BB, 111, true, "CH4"],
		["molten_iron",       "Molten Iron",         0xFF6600, 1811, false, "Fe"],
		["molten_gold",       "Molten Gold",         0xFFD700, 1337, false, "Au"],
		["molten_copper",     "Molten Copper",       0xFF7F24, 1358, false, "Cu"],
		["molten_tin",        "Molten Tin",          0xD3D3D3, 505, false, "Sn"],
		["molten_lead",       "Molten Lead",         0x4A3B5C, 601, false, "Pb"],
		["mercury",           "Mercury",             0xD0D0D0, 234, false, "Hg"],
		["distilled_water",   "Distilled Water",     0x3355FF, 300, false, "H2O"],
	]

	for d: Array in fluid_data:
		var suffix: String = d[0]
		var entry_name: String = d[1]
		var col: int = d[2]
		var temp: int = d[3]
		var is_gas: bool = d[4]
		var formula: String = d[5]

		var eid := "fluid." + suffix
		var color := Color.hex(col)
		var state_str := "Gas" if is_gas else "Liquid"
		var subtitle := state_str + " · Temperature: " + _temp_str(temp, true)

		var lines: Array[String] = [entry_name + " (" + _formula_display(formula) + ")."]
		lines.append("Type: " + state_str + ".")
		lines.append("Standard temperature: " + _temp_str(temp, true) + ".")

		var props: Array[Dictionary] = [
			{"label": "State", "value": state_str},
			{"label": "Formula", "value": _formula_display(formula)},
			{"label": "Temperature", "value": _temp_str(temp, true)},
		]

		_reg(eid, "fluids", color, entry_name, subtitle, "\n".join(lines), props)


# ── Item registration ────────────────────────────────────────────────────
func _register_items() -> void:
	var item_db: ItemDatabase = get_node_or_null("/root/ItemDatabase") as ItemDatabase
	if not item_db:
		push_warning("WikiDatabase: ItemDatabase autoload not found.")
		return

	# Register basic tools and survival items via ItemDatabase.
	_register_item_from_db(item_db, item_db.ITEM_WOODEN_PICKAXE, "items", "Tool",
		"A basic pickaxe made of wood. Suitable for mining soft materials like stone.",
		{"label": "Tier", "value": "Wood"},
		{"label": "Usage", "value": "Mining"},
	)
	_register_item_from_db(item_db, item_db.ITEM_STONE_PICKAXE, "items", "Tool",
		"A stone pickaxe. More durable than wood and can mine harder materials.",
		{"label": "Tier", "value": "Stone"},
		{"label": "Usage", "value": "Mining"},
	)
	_register_item_from_db(item_db, item_db.ITEM_IRON_PICKAXE, "items", "Tool",
		"An iron pickaxe. Strong and durable for most mining tasks.",
		{"label": "Tier", "value": "Iron"},
		{"label": "Usage", "value": "Mining"},
	)
	_register_item_from_db(item_db, item_db.ITEM_WOODEN_AXE, "items", "Tool",
		"A basic wooden axe. Used for chopping wood.",
		{"label": "Tier", "value": "Wood"},
		{"label": "Usage", "value": "Chopping"},
	)
	_register_item_from_db(item_db, item_db.ITEM_STONE_AXE, "items", "Tool",
		"A stone axe. More durable than its wooden counterpart.",
		{"label": "Tier", "value": "Stone"},
		{"label": "Usage", "value": "Chopping"},
	)
	_register_item_from_db(item_db, item_db.ITEM_IRON_AXE, "items", "Tool",
		"An iron axe. Excellent for harvesting wood.",
		{"label": "Tier", "value": "Iron"},
		{"label": "Usage", "value": "Chopping"},
	)
	_register_item_from_db(item_db, item_db.ITEM_WOODEN_SHOVEL, "items", "Tool",
		"A basic wooden shovel for digging soft ground.",
		{"label": "Tier", "value": "Wood"},
		{"label": "Usage", "value": "Digging"},
	)
	_register_item_from_db(item_db, item_db.ITEM_STONE_SHOVEL, "items", "Tool",
		"A stone shovel. More durable than wood.",
		{"label": "Tier", "value": "Stone"},
		{"label": "Usage", "value": "Digging"},
	)
	_register_item_from_db(item_db, item_db.ITEM_IRON_SHOVEL, "items", "Tool",
		"An iron shovel. Designed for efficient earthmoving.",
		{"label": "Tier", "value": "Iron"},
		{"label": "Usage", "value": "Digging"},
	)
	_register_item_from_db(item_db, item_db.ITEM_WOODEN_SWORD, "items", "Weapon",
		"A basic wooden sword for self-defense.",
		{"label": "Tier", "value": "Wood"},
		{"label": "Usage", "value": "Combat"},
	)
	_register_item_from_db(item_db, item_db.ITEM_STONE_SWORD, "items", "Weapon",
		"A stone sword. Better damage than wood.",
		{"label": "Tier", "value": "Stone"},
		{"label": "Usage", "value": "Combat"},
	)
	_register_item_from_db(item_db, item_db.ITEM_IRON_SWORD, "items", "Weapon",
		"An iron sword. A reliable melee weapon.",
		{"label": "Tier", "value": "Iron"},
		{"label": "Usage", "value": "Combat"},
	)
	_register_item_from_db(item_db, item_db.ITEM_WORKBENCH, "items", "Station",
		"A crafting station. Allows crafting of advanced recipes that require tools.",
		{"label": "Type", "value": "Crafting Station"},
	)
	_register_item_from_db(item_db, item_db.ITEM_FURNACE, "items", "Station",
		"A stone furnace. Used for smelting ores and cooking materials at high temperature.",
		{"label": "Type", "value": "Smelting Station"},
	)
	_register_item_from_db(item_db, item_db.ITEM_LADDER, "items", "Structure",
		"A wooden ladder. Placeable for vertical movement.",
		{"label": "Type", "value": "Structure"},
	)

	# GT tools (non-material items).
	var gt_tools: Array = [
		[item_db.ITEM_GT_HAMMER, "GT Hammer", 0xC0C0C0,
			"A GregTech hammer. Essential tool for crafting plates and machine parts."],
		[item_db.ITEM_GT_SAW, "GT Saw", 0xB0A090,
			"A GregTech saw. Used for cutting and precision crafting."],
	]
	for t: Array in gt_tools:
		var item_id: int = t[0]
		var item_def: ItemDef = item_db.get_item(item_id)
		if item_def:
			var eid := "item.tool_" + str(item_id)
			var icon: Texture2D = item_def.icon if item_def.icon else make_placeholder_icon(Color.hex(int(str(t[2]))))
			var e := WikiEntry.new()
			e.id = eid
			e.category = "items"
			e.icon = icon
			e.title = t[1]
			e.subtitle = "Crafting Tool"
			e.description = t[3]
			entries[eid] = e
			if not _category_order.has("items"):
				_category_order["items"] = PackedStringArray()
			var arr: PackedStringArray = _category_order["items"]
			_category_order["items"] = arr + PackedStringArray([eid])


func _register_item_from_db(item_db: ItemDatabase, item_id: int, cat: String,
		subtitle: String, desc: String, prop1: Dictionary, prop2: Dictionary = {}) -> void:
	var item_def: ItemDef = item_db.get_item(item_id)
	if not item_def:
		return

	var eid := "item." + str(item_id)
	var icon: Texture2D = item_def.icon if item_def.icon else make_placeholder_icon(Color.GRAY)
	var e := WikiEntry.new()
	e.id = eid
	e.category = cat
	e.icon = icon
	e.title = tr(item_def.title_key)
	e.subtitle = subtitle

	var props: Array[Dictionary] = []
	if not prop1.is_empty():
		props.append(prop1)
	if not prop2.is_empty():
		props.append(prop2)

	# Add tool stats if available.
	var tool_stats: ToolDef = item_db.get_tool_stats(item_id)
	if tool_stats:
		var _tier_names := {0: "Wood", 1: "Stone", 2: "Iron", 3: "Diamond"}
		var _type_names := {0: "N/A", 1: "Pickaxe", 2: "Axe", 3: "Shovel", 4: "Sword"}
		if tool_stats.speed > 0:
			props.append({"label": "Speed", "value": "%.1f" % tool_stats.speed})
		if tool_stats.durability > 0:
			props.append({"label": "Durability", "value": str(tool_stats.durability)})
		if tool_stats.mining_level > 0:
			props.append({"label": "Mining Level", "value": str(tool_stats.mining_level)})
		if tool_stats.attack_damage > 0:
			props.append({"label": "Attack", "value": "%.1f" % tool_stats.attack_damage})

	e.description = desc
	e.properties = props
	entries[eid] = e
	if not _category_order.has(cat):
		_category_order[cat] = PackedStringArray()
	var arr: PackedStringArray = _category_order[cat]
	_category_order[cat] = arr + PackedStringArray([eid])


# ── Magic registration ───────────────────────────────────────────────────
func _register_magic() -> void:
	# Runes overview.
	var rune_colors := {
		"fire":  0xFF4400, "water": 0x4488FF, "earth": 0x886633, "air":   0xCCFFCC,
		"light": 0xFFFF88, "dark":  0x444466, "order": 0xFFFFFF, "chaos": 0x664488,
	}
	for element: String in rune_colors:
		var ename: String = element
		var col: int = rune_colors[element]
		var cap_name := ename[0].to_upper() + ename.substr(1)
		_reg("magic.runes_" + ename, "magic", Color.hex(col),
			cap_name + " Runes", "Rune · 4 Tiers",
			cap_name + " runes are one of the eight elemental rune types. "
			+ "Each element has four tiers: Common, Refined, Superior, and Legendary. "
			+ "Higher tier runes provide greater potency when used in spell crafting.",
			[{"label": "Element", "value": cap_name}, {"label": "Tiers", "value": "Common, Refined, Superior, Legendary"}])

	# Glyphs overview.
	_reg("magic.glyphs", "magic", Color(0xCC88FF),
		"Glyphs", "Spell Component · 69 Total",
		"Glyphs are the building blocks of spells. They come in three slot types:\n"
		+ "- Form Glyphs (5): Determine the spell's shape (Projectile, Self, Area, Beam, Touch).\n"
		+ "- Effect Glyphs (32): Determine the spell's elemental effect, one per element+tier.\n"
		+ "- Augment Glyphs (32): Enhance spell power, one per element+tier.\n\n"
		+ "Compile glyphs in a Spell Book to create custom spells.",
		[{"label": "Total Glyphs", "value": "69"},
		 {"label": "Form Glyphs", "value": "5 (Projectile, Self, Area, Beam, Touch)"},
		 {"label": "Effect Glyphs", "value": "32 (8 elements x 4 tiers)"},
		 {"label": "Augment Glyphs", "value": "32 (8 elements x 4 tiers)"}])

	# Rituals overview.
	_reg("magic.rituals", "magic", Color(0xFF88AA),
		"Rituals", "Altar Magic",
		"Rituals are performed at altars by placing runes on pedestals. "
		+ "There are two types of altars:\n"
		+ "- Basic Altar: 4 pedestals (cardinal directions).\n"
		+ "- Advanced Altar: 8 pedestals (all directions).\n\n"
		+ "Ritual effects include machine blessing, tool enchantment, player buffs, "
		+ "teleportation, mana expansion, and more.",
		[{"label": "Altar Types", "value": "Basic (4 pedestals), Advanced (8 pedestals)"},
		 {"label": "Ritual Effects", "value": "Blessing, Enchantment, Buff, Teleport, etc."}])

	# Mana overview.
	_reg("magic.mana", "magic", Color(0x4488FF),
		"Mana", "Magic Resource",
		"Mana is the energy that powers spells and rituals. "
		+ "Your mana pool determines how many spells you can cast before needing to recharge. "
		+ "Mana regenerates slowly over time and can be restored with mana potions.",
		[{"label": "Resource Type", "value": "Spell Casting Energy"},
		 {"label": "Regeneration", "value": "Passive over time"},
		 {"label": "Restoration", "value": "Mana potions, rituals"}])


# ── Guides registration ───────────────────────────────────────────────────
func _register_guides() -> void:
	_reg("guide.welcome", "guides", Color(0x44AA44),
		"Getting Started", "Beginner Guide",
		"Welcome to Science & Theology!\n\n"
		+ "You have arrived in a world where science and magic coexist. "
		+ "Start by gathering basic resources like wood and stone.\n\n"
		+ "Tips for beginners:\n"
		+ "- Use WASD to move and the mouse to interact with the world.\n"
		+ "- Press E to open your inventory.\n"
		+ "- Press C to open the crafting menu.\n"
		+ "- Press B to open this encyclopedia.\n"
		+ "- Mine stone, chop wood, and gather resources to craft your first tools.",
		[{"label": "Controls", "value": "WASD / Mouse"},
		 {"label": "Inventory", "value": "E"},
		 {"label": "Crafting", "value": "C"},
		 {"label": "Encyclopedia", "value": "B"}])

	_reg("guide.mining", "guides", Color(0x888888),
		"Mining Guide", "Resource Gathering",
		"Mining is the primary way to obtain resources. "
		+ "Different materials require different pickaxe tiers:\n\n"
		+ "- Wood Pickaxe: Mines stone, coal, and soft materials.\n"
		+ "- Stone Pickaxe: Mines copper, iron, and moderate materials.\n"
		+ "- Iron Pickaxe: Mines harder ores and all common materials.\n\n"
		+ "Use the right tool for the job to mine efficiently. "
		+ "Some rare materials may require even higher tier tools.",
		[{"label": "Key Tools", "value": "Pickaxe (wood/stone/iron)"},
		 {"label": "Best Practice", "value": "Match tool tier to material"}])

	_reg("guide.crafting", "guides", Color(0xCC8844),
		"Crafting Guide", "Creating Items",
		"Crafting allows you to transform raw materials into useful items.\n\n"
		+ "- Hand Crafting: Available anywhere without a station.\n"
		+ "- Workbench: Required for advanced recipes using tools.\n"
		+ "- Furnace: Smelts ores into ingots at high temperature.\n\n"
		+ "Open the crafting menu (C) to browse recipes by category. "
		+ "The required station and tools are shown for each recipe. "
		+ "Progress through material tiers by smelting basic ores and crafting better tools.",
		[{"label": "Stations", "value": "Hand, Workbench, Furnace"},
		 {"label": "Categories", "value": "Materials, Tools, Parts, Wires, Cables, Circuits, Machines"}])

	_reg("guide.power", "guides", Color(0xFFCC00),
		"Power System Overview", "Energy Network",
		"The power system is inspired by GregTech's voltage tier system.\n\n"
		+ "Voltage Tiers (low to high):\n"
		+ "ULV → LV → MV → HV → EV → IV → LuV → ZPM → UV → UHV → UEV → UIV → UMV → UXV → MAX\n\n"
		+ "Machines operate at specific voltage tiers and consume EU (Energy Units) per tick. "
		+ "Connect machines to power networks with cables matching the voltage tier. "
		+ "Higher tier machines process recipes faster through overclocking.\n\n"
		+ "Warning: Connecting low-tier machines to high-voltage power will destroy them!",
		[{"label": "Tiers", "value": "15 tiers (ULV to MAX)"},
		 {"label": "Unit", "value": "EU/t (Energy Units per tick)"},
		 {"label": "Warning", "value": "Match voltage tier of machines and cables"}])

	_reg("guide.magic_intro", "guides", Color(0x8844FF),
		"Introduction to Magic", "Spell System",
		"The magic system allows you to create and cast custom spells.\n\n"
		+ "Components:\n"
		+ "- Runes: Elemental items placed on altar pedestals for rituals.\n"
		+ "- Glyphs: Spell components placed in your Spell Book.\n"
		+ "- Spell Book: Where you compile glyphs into castable spells.\n"
		+ "- Mana: Magical energy consumed when casting spells.\n\n"
		+ "To create a spell, open your Spell Book and place glyphs in the slots:\n"
		+ "1. Choose a Form Glyph (Projectile, Self, Area, Beam, Touch).\n"
		+ "2. Choose an Effect Glyph (determines the element and effect).\n"
		+ "3. Optionally add Augment Glyphs to increase potency.\n\n"
		+ "Rituals are performed at altars by placing runes on pedestals in specific arrangements.",
		[{"label": "Key Item", "value": "Spell Book"},
		 {"label": "Spell Parts", "value": "Form + Effect + Augment Glyphs"},
		 {"label": "Ritual Parts", "value": "Runes on pedestals at Altar"}])
