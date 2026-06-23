# AI 图标转 64×64 像素资产流程

本文档说明如何把 AI 生成的大尺寸图标转成 ScienceAndTheology 项目可用的 64×64 / 32×32 像素风物品图标。

对应脚本：

```text
/tools/pixel_repaint.py
```

## 1. 安装依赖

```bash
pip install pillow
```

## 2. 推荐目录结构

建议不要把 AI 原图直接放进正式资源目录，而是单独放到 `raw_assets/`：

```text
raw_assets/
  item_icons/
    copper_pickaxe.png
    iron_ore.png
    clay_ball.png

resource/
  items/
    tools/
    materials/
    tfc/
    crops/
    source_law/
```

`raw_assets/` 可以加入 `.gitignore`，只提交处理后的 `resource/items/...`。

## 3. 单张图片处理

例如把铜镐 AI 图处理成 64×64，并额外生成 32×32：

```bash
python tools/pixel_repaint.py \
  --input raw_assets/item_icons/copper_pickaxe.png \
  --output resource/items/tools \
  --name copper_pickaxe \
  --size 64 \
  --also-32 \
  --remove-bg auto \
  --palette adaptive \
  --colors 24 \
  --outline
```

输出：

```text
resource/items/tools/copper_pickaxe_icon_64.png
resource/items/tools/copper_pickaxe_icon_32.png
```

## 4. 批量处理

```bash
python tools/pixel_repaint.py \
  --input raw_assets/item_icons \
  --output resource/items/_processed \
  --recursive \
  --size 64 \
  --also-32 \
  --remove-bg auto \
  --palette adaptive \
  --colors 24 \
  --outline
```

## 5. 关键参数说明

### `--remove-bg auto`

用于去掉 AI 生成图里常见的“透明棋盘背景”。

注意：有些图片看起来是透明背景，但实际已经把棋盘烘焙进像素里。这个脚本会从图片边缘做 flood fill，只删除和边缘连通的浅色棋盘背景，尽量避免误删物体内部高光。

如果原图本来就是透明 PNG，可以用：

```bash
--remove-bg none
```

### `--palette adaptive`

按单张图片自适应限色。适合大多数 AI 图标。

推荐颜色数：

| 类型 | colors |
|---|---:|
| 简单材料 | 16 |
| 普通物品 | 24 |
| 复杂魔法物 | 32 |

### `--palette snt`

强制映射到项目统一调色板，适合风格收束，但可能让某些 AI 图标颜色损失较大。

建议先用：

```bash
--palette adaptive --colors 24
```

等图标大体可用后，再挑需要统一的图试：

```bash
--palette snt
```

### `--outline`

给图标自动加 1px 深色外描边。适合物品图标，但如果原图已经有非常粗的描边，可以不加。

默认描边色：

```text
#1A1410
```

### `--contrast` / `--saturation`

默认会轻微提高对比度和饱和度：

```text
contrast = 1.08
saturation = 1.03
```

如果某张图已经很亮，可以调低：

```bash
--contrast 1.0 --saturation 1.0
```

## 6. 接入 ItemDatabase.gd

脚本输出的路径要相对于 `res://resource/items/`。

例如生成：

```text
resource/items/tools/copper_pickaxe_icon_64.png
```

那么 `ItemDatabase.gd` 里应该写：

```gdscript
"tools/copper_pickaxe_icon_64.png"
```

不要写完整 `res://resource/items/...`，因为当前 `ItemDatabase.ITEM_ASSET_DIR` 已经是：

```gdscript
const ITEM_ASSET_DIR := "res://resource/items/"
```

## 7. 推荐工作流

```text
AI 生成大图
→ 放入 raw_assets/item_icons/
→ pixel_repaint.py 自动裁切/去背景/缩放/限色/描边
→ 输出到 resource/items/...
→ Godot 导入资源
→ 修改 ItemDatabase.gd 的 icon_file
→ 游戏内检查 64×64 和 32×32 可读性
→ 必要时用 Aseprite 手工修边
```

## 8. 注意事项

这个脚本是“自动重绘辅助”，不是最终美术替代品。

它能解决：

- AI 图尺寸太大
- 背景不是透明
- 颜色太多
- 边缘太软
- 需要 64×64 / 32×32 输出

它不能完美解决：

- 物体轮廓本身不清楚
- AI 图标主体画错
- 过多细节在 64×64 下不可读
- 风格完全不统一

最终高质量图标仍建议用 Aseprite 做一次人工清边和删减细节。
