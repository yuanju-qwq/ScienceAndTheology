class_name BuiltinGlyphs
extends RefCounted

# Built-in glyph definitions, migrated from C++ GlyphRegistry::register_builtin_glyphs().
# 5 form + 32 effect + 32 augment = 69 glyphs.

# 显式确定性 ID（P1: 热重载后 ID 不漂移）：
#   form    → form + 1                       (1-5)
#   effect  → 64 + element * 8 + tier + 1    (65-96)
#   augment → 128 + element * 8 + tier + 1   (129-160)
# ID 0 保留给 invalid；每段预留 64 位空间便于扩展。

# GlyphSlotType: FORM=0, EFFECT=1, AUGMENT=2
# SpellForm: PROJECTILE=0, SELF=1, AREA=2, BEAM=3, TOUCH=4
# RuneElement: FIRE=0, WATER=1, EARTH=2, AIR=3, LIGHT=4, DARK=5, ORDER=6, CHAOS=7
# RuneTier: COMMON=0, REFINED=1, SUPERIOR=2, LEGENDARY=3
# potency: COMMON=1, REFINED=2, SUPERIOR=3, LEGENDARY=5

const _ELEMENT_NAMES := [
	"fire", "water", "earth", "air", "light", "dark", "order", "chaos"
]
const _TIER_NAMES := ["common", "refined", "superior", "legendary"]
const _TIER_POTENCY := [1, 2, 3, 5]
const _FORM_NAMES := ["projectile", "self", "area", "beam", "touch"]


static func register_all() -> void:
	_register_form_glyphs()
	_register_effect_augment_glyphs()


static func _register_form_glyphs() -> void:
	for form in range(5):
		GDGlyphRegistry.register_glyph({
			"id": form + 1,
			"name": "glyph_form_%s" % _FORM_NAMES[form],
			"slot_type": 0,  # FORM
			"element": 0,    # FIRE (placeholder)
			"tier": 0,       # COMMON
			"potency": 1,
			"form": form,
		})


static func _register_effect_augment_glyphs() -> void:
	for element in range(8):
		for tier in range(4):
			var e_name := "glyph_effect_%s_%s" % [_ELEMENT_NAMES[element], _TIER_NAMES[tier]]
			GDGlyphRegistry.register_glyph({
				"id": 64 + element * 8 + tier + 1,
				"name": e_name,
				"slot_type": 1,  # EFFECT
				"element": element,
				"tier": tier,
				"potency": _TIER_POTENCY[tier],
				"form": 0,       # PROJECTILE (placeholder)
			})

			var a_name := "glyph_augment_%s_%s" % [_ELEMENT_NAMES[element], _TIER_NAMES[tier]]
			GDGlyphRegistry.register_glyph({
				"id": 128 + element * 8 + tier + 1,
				"name": a_name,
				"slot_type": 2,  # AUGMENT
				"element": element,
				"tier": tier,
				"potency": _TIER_POTENCY[tier],
				"form": 0,       # PROJECTILE (placeholder)
			})
