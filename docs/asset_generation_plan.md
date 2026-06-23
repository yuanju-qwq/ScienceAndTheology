# ScienceAndTheology 美术资产生成计划

> 目标：把当前项目中由程序色块、BoxMesh、fallback placeholder icon、纯色材质承担的视觉表现，逐步替换为稳定、可命名、可维护的正式资产。

当前结论：项目不是完全没有素材，已经存在少量 PNG 图标和少量地形贴图；但主流程视觉仍以程序生成、纯色材质、BoxMesh、fallback 图标为主。

## 资产命名规范

### 物品图标

路径建议：

```text
resource/items/<category>/<item_key>_icon_32.png
resource/items/<category>/<item_key>_icon_64.png
```

建议优先使用 32×32 PNG，树木类、特殊掉落物和高辨识度物品可同步准备 64×64。

命名示例：

```text
resource/items/tfc/clay_ball_icon_32.png
resource/items/tfc/unfired_crucible_icon_32.png
resource/items/source_law/glow_deer_antler_icon_32.png
resource/items/crops/wheat_seed_icon_32.png
resource/items/tools/copper_pickaxe_icon_32.png
```

### 方块贴图

路径建议：

```text
resource/terrain/<material_key>/<material_key>_tile_01_32.png
resource/terrain/<material_key>/<material_key>_tile_02_32.png
resource/terrain/<material_key>/<material_key>_tile_03_32.png
resource/terrain/<material_key>/<material_key>_tile_04_32.png
```

方块贴图建议：

- 32×32 起步。
- 能平铺。
- 尽量保持体素游戏的块面可读性。
- 同一材料可以准备 1~4 个变体。

### 3D 模型

路径建议：

```text
resource/models/blocks/<model_key>.glb
resource/models/machines/<machine_key>.glb
resource/models/creatures/<species_key>/<species_key>.glb
```

建议优先低模风格，和体素方块世界统一，不要过度写实。

---

## P0：第一批必须生成

### P0-1 物品图标

优先目标：先替换玩家最常见、最容易在背包/热键栏看到的占位图。

#### 工具与早期生存

- [ ] `tools/copper_pickaxe_icon_32.png`
- [ ] `tools/copper_axe_icon_32.png`
- [ ] `tools/copper_shovel_icon_32.png`
- [ ] `tools/copper_sword_icon_32.png`
- [ ] `tools/tin_bronze_pickaxe_icon_32.png`
- [ ] `tools/tin_bronze_axe_icon_32.png`
- [ ] `tools/bismuth_bronze_pickaxe_icon_32.png`
- [ ] `tools/bismuth_bronze_axe_icon_32.png`
- [ ] `tools/black_bronze_pickaxe_icon_32.png`
- [ ] `tools/steel_pickaxe_icon_32.png`
- [ ] `tools/steel_axe_icon_32.png`
- [ ] `tools/steel_shovel_icon_32.png`
- [ ] `tools/steel_sword_icon_32.png`
- [ ] `tools/stone_hoe_icon_32.png`
- [ ] `tools/stone_knife_icon_32.png`
- [ ] `tools/stone_axe_head_icon_32.png`
- [ ] `tools/stone_shovel_head_icon_32.png`
- [ ] `tools/stone_hoe_head_icon_32.png`
- [ ] `tools/stone_knife_head_icon_32.png`
- [ ] `tools/flint_icon_32.png`
- [ ] `tools/chert_icon_32.png`

#### TFC 生存 / 陶器 / 冶金

- [ ] `tfc/clay_ball_icon_32.png`
- [ ] `tfc/straw_icon_32.png`
- [ ] `tfc/charcoal_icon_32.png`
- [ ] `tfc/coal_dust_icon_32.png`
- [ ] `tfc/flint_and_steel_icon_32.png`
- [ ] `tfc/unfired_bowl_icon_32.png`
- [ ] `tfc/unfired_jug_icon_32.png`
- [ ] `tfc/unfired_crucible_icon_32.png`
- [ ] `tfc/unfired_brick_icon_32.png`
- [ ] `tfc/fired_bowl_icon_32.png`
- [ ] `tfc/fired_jug_icon_32.png`
- [ ] `tfc/fired_crucible_icon_32.png`
- [ ] `tfc/refractory_brick_icon_32.png`
- [ ] `tfc/iron_bloom_icon_32.png`
- [ ] `tfc/wrought_iron_ingot_icon_32.png`
- [ ] `tfc/steel_ingot_icon_32.png`
- [ ] `tfc/hammer_icon_32.png`
- [ ] `tfc/bellows_icon_32.png`
- [ ] `tfc/anvil_icon_32.png`

#### 源律 / 生物掉落物

- [ ] `source_law/glow_deer_antler_icon_32.png`
- [ ] `source_law/purifying_pollen_icon_32.png`
- [ ] `source_law/rock_lizard_scale_icon_32.png`
- [ ] `source_law/crystallized_bone_powder_icon_32.png`
- [ ] `source_law/thunderbird_feather_icon_32.png`
- [ ] `source_law/magnetic_crystal_shard_icon_32.png`
- [ ] `source_law/sea_serpent_scale_icon_32.png`
- [ ] `source_law/tidal_gland_icon_32.png`
- [ ] `source_law/blazing_core_icon_32.png`
- [ ] `source_law/molten_blood_sample_icon_32.png`
- [ ] `source_law/aether_fragment_icon_32.png`
- [ ] `source_law/blueprint_shard_icon_32.png`
- [ ] `source_law/aberrant_organ_icon_32.png`
- [ ] `source_law/polluted_source_essence_icon_32.png`

#### 作物与食物

- [ ] `crops/wheat_seed_icon_32.png`
- [ ] `crops/wheat_crop_icon_32.png`
- [ ] `crops/carrot_seed_icon_32.png`
- [ ] `crops/carrot_crop_icon_32.png`
- [ ] `crops/potato_seed_icon_32.png`
- [ ] `crops/potato_crop_icon_32.png`
- [ ] `crops/cotton_seed_icon_32.png`
- [ ] `crops/cotton_crop_icon_32.png`
- [ ] `crops/herb_seed_icon_32.png`
- [ ] `crops/herb_crop_icon_32.png`
- [ ] `crops/pumpkin_seed_icon_32.png`
- [ ] `crops/pumpkin_crop_icon_32.png`
- [ ] `crops/bone_meal_icon_32.png`
- [ ] `crops/flour_icon_32.png`
- [ ] `crops/bread_icon_32.png`
- [ ] `crops/cotton_fiber_icon_32.png`
- [ ] `crops/cloth_icon_32.png`

#### 特殊放置物

- [ ] `placeables/fence_icon_32.png`
- [ ] `placeables/station_blueprint_icon_32.png`

---

### P0-2 核心方块贴图

先做最常见的 12 类：

- [ ] `stone/stone_tile_01_32.png`
- [ ] `dirt/dirt_tile_01_32.png`
- [ ] `sand/sand_tile_01_32.png`：已有，可作为质量基准或重制
- [ ] `water/water_tile_01_32.png`
- [ ] `wood/wood_tile_01_32.png`
- [ ] `leaves/leaves_tile_01_32.png`
- [ ] `ore_coal/ore_coal_tile_01_32.png`
- [ ] `ore_copper/ore_copper_tile_01_32.png`
- [ ] `ore_iron/ore_iron_tile_01_32.png`
- [ ] `workbench/workbench_tile_01_32.png`
- [ ] `stone_furnace/stone_furnace_tile_01_32.png`
- [ ] `farmland/farmland_tile_01_32.png`

---

### P0-3 生物模型

当前生物渲染为 BoxMesh + Eye。第一批需要替换 7 个源律生物：

- [ ] `models/creatures/glow_deer/glow_deer.glb`
- [ ] `models/creatures/rock_lizard/rock_lizard.glb`
- [ ] `models/creatures/thunderbird/thunderbird.glb`
- [ ] `models/creatures/sea_serpent/sea_serpent.glb`
- [ ] `models/creatures/blaze_beast/blaze_beast.glb`
- [ ] `models/creatures/aether_wraith/aether_wraith.glb`
- [ ] `models/creatures/aberrant_ascended/aberrant_ascended.glb`

每个生物建议至少包含：

- idle
- walk / fly / swim
- hurt
- death
- attack

---

### P0-4 世界物体 / 机器模型

当前 `magic_structure` 明确是 placeholder；`furnace` 也是 BoxMesh 组合。第一批建议：

- [ ] `models/blocks/stone_furnace.glb`
- [ ] `models/blocks/workbench.glb`
- [ ] `models/blocks/ladder.glb`
- [ ] `models/blocks/fence.glb`
- [ ] `models/blocks/magic_structure.glb`
- [ ] `models/blocks/pit_kiln.glb`
- [ ] `models/blocks/log_pile.glb`
- [ ] `models/blocks/bloomery.glb`
- [ ] `models/blocks/anvil.glb`
- [ ] `models/machines/basic_machine_hull.glb`
- [ ] `models/machines/lv_machine_generic.glb`
- [ ] `models/space/station_core.glb`

---

### P0-5 UI 基础皮肤

当前 UI 大多由 ColorRect / Label / _draw 构建。第一批建议：

- [ ] `ui/logo_science_and_theology.png`
- [ ] `ui/main_menu_background.png`
- [ ] `ui/button_normal.9.png`
- [ ] `ui/button_hover.9.png`
- [ ] `ui/button_pressed.9.png`
- [ ] `ui/panel_background.9.png`
- [ ] `ui/tooltip_background.9.png`
- [ ] `ui/slot_normal.png`
- [ ] `ui/slot_selected.png`
- [ ] `ui/slot_hover.png`
- [ ] `ui/crosshair_default.png`
- [ ] `ui/icon_inventory.png`
- [ ] `ui/icon_wiki.png`
- [ ] `ui/icon_quest_book.png`
- [ ] `ui/icon_crafting.png`

---

## P1：补齐完整资产体系

### 行星岩石粉图标

- [ ] granite dust / tiny dust
- [ ] basalt dust / tiny dust
- [ ] marble dust / tiny dust
- [ ] sandstone dust / tiny dust
- [ ] shale dust / tiny dust
- [ ] komatiite dust / tiny dust
- [ ] regolith dust / tiny dust
- [ ] anorthosite dust / tiny dust

### 全矿石方块贴图

- [ ] tin
- [ ] zinc
- [ ] lead
- [ ] silver
- [ ] gold
- [ ] nickel
- [ ] bauxite
- [ ] manganese
- [ ] tungsten
- [ ] titanium
- [ ] platinum
- [ ] cobalt
- [ ] uranium
- [ ] sulfur
- [ ] diamond
- [ ] ruby
- [ ] sapphire
- [ ] emerald
- [ ] salt
- [ ] fluorite
- [ ] graphite
- [ ] pyrite
- [ ] galena
- [ ] cinnabar
- [ ] magnetite
- [ ] cassiterite
- [ ] ilmenite
- [ ] chalcopyrite
- [ ] sphalerite
- [ ] pentlandite

### 树种方块贴图

每种树至少三套：log / leaves / sapling。

- [ ] oak
- [ ] birch
- [ ] spruce
- [ ] acacia
- [ ] maple
- [ ] sequoia
- [ ] cherry
- [ ] olive

### 作物阶段方块贴图

每种作物四阶段：seed / sprout / growing / mature。

- [ ] wheat
- [ ] carrot
- [ ] potato
- [ ] cotton
- [ ] herb
- [ ] pumpkin

---

## P2：太空与星球风格化

当前星球远景主要是程序 shader，可运行但不是最终风格。后续可以补：

- [ ] 类地星球视觉预设
- [ ] 荒漠星球视觉预设
- [ ] 冰冻星球视觉预设
- [ ] 熔岩星球视觉预设
- [ ] 气态巨行星视觉预设
- [ ] 星环资源
- [ ] 星空 / 星云背景资源

---

## 接入原则

1. 先提交资源文件，再改 `ItemDatabase.gd` / `BuiltinTerrainContent.gd` / `BuiltinBlockModels.gd` 的引用。
2. 不要删除 fallback placeholder 机制，它仍然适合开发期容错。
3. 图标路径必须和 `ItemDatabase.ITEM_ASSET_DIR = res://resource/items/` 匹配。
4. 方块贴图路径必须和 terrain visual 注册匹配。
5. 3D 模型先用低模，不要一次性追求高复杂度。
6. 生物模型先支持静态模型，再接入动画状态机。

---

## 第一阶段完成定义

P0 完成后，项目应达到：

- 背包/热键栏核心物品不再出现棋盘格 fallback 图标。
- 地表最常见方块不再全部依赖纯色材质。
- 7 种源律生物不再是彩色方块。
- magic_structure 不再是明确 placeholder。
- 主菜单、背包、热键栏有统一 UI 皮肤基调。
