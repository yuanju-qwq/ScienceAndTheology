class_name BuiltinFluids
extends RefCounted

# Built-in fluid definitions, migrated from C++ FluidRegistry::register_builtin_fluids().
# Registration is global and must happen before world load (before fuel
# registration, since fluid fuels reference fluid ids by name).

# Each entry: {name, title_key, chemical_formula, temperature, is_gas}
const _ALL_FLUIDS := [
	# --- Basic fluids ---
	{name: "water",            title_key: "fluid.water",            chemical_formula: "H2O",      temperature: 300,  is_gas: false},
	{name: "lava",             title_key: "fluid.lava",             chemical_formula: "?",        temperature: 1500, is_gas: false},
	{name: "steam",            title_key: "fluid.steam",            chemical_formula: "H2O",      temperature: 400,  is_gas: true},

	# --- Oil processing ---
	{name: "oil",              title_key: "fluid.oil",              chemical_formula: "?",        temperature: 300,  is_gas: false},
	{name: "oil_heavy",        title_key: "fluid.oil_heavy",        chemical_formula: "?",        temperature: 350,  is_gas: false},
	{name: "oil_light",        title_key: "fluid.oil_light",        chemical_formula: "?",        temperature: 250,  is_gas: false},

	# --- Fuels ---
	{name: "fuel_diesel",      title_key: "fluid.fuel_diesel",      chemical_formula: "?",        temperature: 250,  is_gas: false},
	{name: "fuel_rocket",      title_key: "fluid.fuel_rocket",      chemical_formula: "?",        temperature: 200,  is_gas: false},
	{name: "ethanol",          title_key: "fluid.ethanol",          chemical_formula: "C2H5OH",   temperature: 300,  is_gas: false},

	# --- Acids ---
	{name: "sulfuric_acid",    title_key: "fluid.sulfuric_acid",    chemical_formula: "H2SO4",    temperature: 300,  is_gas: false},
	{name: "hydrochloric_acid",title_key: "fluid.hydrochloric_acid",chemical_formula: "HCl",      temperature: 300,  is_gas: false},
	{name: "nitric_acid",      title_key: "fluid.nitric_acid",      chemical_formula: "HNO3",     temperature: 300,  is_gas: false},
	{name: "hydrofluoric_acid",title_key: "fluid.hydrofluoric_acid",chemical_formula: "HF",       temperature: 300,  is_gas: false},
	{name: "aqua_regia",       title_key: "fluid.aqua_regia",       chemical_formula: "HNO3+3HCl",temperature: 300,  is_gas: false},

	# --- Industrial chemicals ---
	{name: "creosote",         title_key: "fluid.creosote",         chemical_formula: "?",        temperature: 400,  is_gas: false},
	{name: "lubricant",        title_key: "fluid.lubricant",        chemical_formula: "?",        temperature: 300,  is_gas: false},
	{name: "glue",             title_key: "fluid.glue",             chemical_formula: "?",        temperature: 300,  is_gas: false},
	{name: "biomass",          title_key: "fluid.biomass",          chemical_formula: "?",        temperature: 300,  is_gas: false},

	# --- Gases ---
	{name: "oxygen",           title_key: "fluid.oxygen",           chemical_formula: "O2",       temperature: 90,   is_gas: true},
	{name: "hydrogen",         title_key: "fluid.hydrogen",         chemical_formula: "H2",       temperature: 20,   is_gas: true},
	{name: "nitrogen",         title_key: "fluid.nitrogen",         chemical_formula: "N2",       temperature: 77,   is_gas: true},
	{name: "natural_gas",      title_key: "fluid.natural_gas",      chemical_formula: "CH4",      temperature: 111,  is_gas: true},

	# --- Molten metals ---
	{name: "molten_iron",      title_key: "fluid.molten_iron",      chemical_formula: "Fe",       temperature: 1811, is_gas: false},
	{name: "molten_gold",      title_key: "fluid.molten_gold",      chemical_formula: "Au",       temperature: 1337, is_gas: false},
	{name: "molten_copper",    title_key: "fluid.molten_copper",    chemical_formula: "Cu",       temperature: 1358, is_gas: false},
	{name: "molten_tin",       title_key: "fluid.molten_tin",       chemical_formula: "Sn",       temperature: 505,  is_gas: false},
	{name: "molten_lead",      title_key: "fluid.molten_lead",      chemical_formula: "Pb",       temperature: 601,  is_gas: false},

	# --- Other ---
	{name: "mercury",          title_key: "fluid.mercury",          chemical_formula: "Hg",       temperature: 234,  is_gas: false},
	{name: "distilled_water",  title_key: "fluid.distilled_water",  chemical_formula: "H2O",      temperature: 300,  is_gas: false},
]


# Register all built-in fluids with the C++ FluidRegistry via GDFluidRegistry.
# Must be called before fuel registration and before world load.
static func register_all() -> void:
	for entry in _ALL_FLUIDS:
		GDFluidRegistry.register_fluid(entry)

	# Phase transition: water evaporates at 373 K (100°C) into steam,
	# steam condenses at 373 K back into water.
	GDFluidRegistry.link_phase_transition("water", "steam", 373, 373)
