[CmdletBinding()]
param(
    [string]$SourcePath = "scripts/worldgen/BuiltinTerrainContent.gd",
    [string]$OutputPath = "game/worldgen/legacy_terrain_material_catalog.cpp"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-QuotedField {
    param([string]$Block, [string]$Name)

    $match = [regex]::Match($Block, '"' + [regex]::Escape($Name) + '"\s*:\s*"([^"]*)"')
    if ($match.Success) {
        return $match.Groups[1].Value
    }
    return ""
}

function Get-NumberField {
    param([string]$Block, [string]$Name, [string]$DefaultValue)

    $match = [regex]::Match($Block, '"' + [regex]::Escape($Name) + '"\s*:\s*(-?[0-9]+(?:\.[0-9]+)?)')
    if ($match.Success) {
        return $match.Groups[1].Value
    }
    return $DefaultValue
}

function Escape-CppString {
    param([string]$Value)

    return $Value.Replace('\', '\\').Replace('"', '\"')
}

function Resolve-FlagExpression {
    param([string]$Expression)

    $trimmed = $Expression.Trim()
    if ($trimmed -eq "0") { return "0u" }
    $result = $trimmed
    foreach ($flag in @("WALKABLE", "SOLID", "LIQUID", "MINEABLE", "CLIMBABLE", "INDESTRUCTIBLE", "GRAVITY_FALL", "COLLAPSE_RISK", "SUPPORT_BEAM")) {
        $result = $result.Replace("FLAG_$flag", "TF_$flag")
    }
    if ($result -match "FLAG_") {
        throw "Unsupported terrain flag expression '$Expression'."
    }
    return $result
}

function Get-BracedBlock {
    param([string]$Text, [int]$StartIndex)

    $openIndex = $Text.IndexOf('{', $StartIndex)
    if ($openIndex -lt 0) { throw "Expected '{' after index $StartIndex." }

    $depth = 0
    $quote = [char]0
    $escaped = $false
    for ($index = $openIndex; $index -lt $Text.Length; ++$index) {
        $character = $Text[$index]
        if ($quote -ne [char]0) {
            if ($escaped) {
                $escaped = $false
            } elseif ($character -eq '\') {
                $escaped = $true
            } elseif ($character -eq $quote) {
                $quote = [char]0
            }
            continue
        }
        if ($character -eq '"' -or $character -eq "'") {
            $quote = $character
            continue
        }
        if ($character -eq '{') {
            ++$depth
            continue
        }
        if ($character -eq '}') {
            --$depth
            if ($depth -eq 0) {
                return $Text.Substring($openIndex, $index - $openIndex + 1)
            }
        }
    }
    throw "Unterminated dictionary starting at index $openIndex."
}

function Get-Drops {
    param([string]$Block)

    $drops = [System.Collections.Generic.List[object]]::new()
    $matches = [regex]::Matches(
        $Block,
        '\{\s*"item_key"\s*:\s*"([^"]+)"(?<properties>[^{}]*)\}')
    foreach ($match in $matches) {
        $properties = $match.Groups["properties"].Value
        $drops.Add([pscustomobject]@{
            ItemKey = $match.Groups[1].Value
            Count = Get-NumberField $properties "count" "1"
            MinCount = Get-NumberField $properties "min_count" "1"
            MaxCount = Get-NumberField $properties "max_count" "1"
            Chance = Get-NumberField $properties "chance" "1.0"
        })
    }
    return $drops
}

function Add-MaterialRecord {
    param(
        [hashtable]$ById,
        [int]$Id,
        [string]$Key,
        [string]$TitleKey,
        [string]$Flags,
        [string]$Hardness,
        [string]$RequiredToolTag = "",
        [string]$RequiredMiningLevel = "0",
        [object[]]$Drops = @(),
        [string]$CollapseChance = "0.3",
        [string]$SupportRadius = "5",
        [string]$RockLayerKey = ""
    )

    if ($ById.ContainsKey($Id)) {
        throw "Duplicate terrain material id $Id ('$Key')."
    }
    if ([string]::IsNullOrWhiteSpace($Key)) {
        throw "Terrain material $Id has an empty key."
    }
    $normalizedDrops = [System.Collections.Generic.List[object]]::new()
    foreach ($drop in $Drops) {
        if ($null -eq $drop) { continue }
        if ($drop -is [System.Collections.IEnumerable] -and -not ($drop -is [string])) {
            foreach ($nestedDrop in $drop) {
                if ($null -ne $nestedDrop) { $normalizedDrops.Add($nestedDrop) }
            }
        } else {
            $normalizedDrops.Add($drop)
        }
    }

    $ById[$Id] = [pscustomobject]@{
        Id = $Id
        Key = $Key
        TitleKey = $TitleKey
        Flags = $Flags
        Hardness = $Hardness
        RequiredToolTag = $RequiredToolTag
        RequiredMiningLevel = $RequiredMiningLevel
        Drops = $normalizedDrops.ToArray()
        CollapseChance = $CollapseChance
        SupportRadius = $SupportRadius
        RockLayerKey = $RockLayerKey
    }
}

$resolvedSource = Resolve-Path -LiteralPath $SourcePath
$source = [System.IO.File]::ReadAllText($resolvedSource)
$constants = @{}
foreach ($match in [regex]::Matches($source, '(?m)^\s*const\s+(MAT_[A-Z0-9_]+)\s*:=\s*(\d+)')) {
    $constants[$match.Groups[1].Value] = [int]$match.Groups[2].Value
}

$interactionStart = $source.IndexOf("static func _register_builtin_material_interactions")
$interactionEnd = $source.IndexOf("static func _register_builtin_material_visuals")
if ($interactionStart -lt 0 -or $interactionEnd -le $interactionStart) {
    throw "Could not locate the legacy terrain material registration section."
}
$interactionSource = $source.Substring($interactionStart, $interactionEnd - $interactionStart)
$materials = @{}

$cursor = 0
while ($true) {
    $callIndex = $interactionSource.IndexOf("registry.register_material({", $cursor)
    if ($callIndex -lt 0) { break }
    $block = Get-BracedBlock $interactionSource $callIndex
    $cursor = $callIndex + $block.Length
    $idMatch = [regex]::Match($block, '"id"\s*:\s*(MAT_[A-Z0-9_]+)')
    if (-not $idMatch.Success) {
        continue
    }
    $symbol = $idMatch.Groups[1].Value
    if (-not $constants.ContainsKey($symbol)) {
        throw "Unknown terrain material symbol '$symbol'."
    }
    $flagsMatch = [regex]::Match($block, '"flags"\s*:\s*([^,\r\n]+)')
    if (-not $flagsMatch.Success) {
        throw "Terrain material '$symbol' has no flags."
    }
    Add-MaterialRecord $materials $constants[$symbol] (Get-QuotedField $block "key") `
        (Get-QuotedField $block "title_key") (Resolve-FlagExpression $flagsMatch.Groups[1].Value) `
        (Get-NumberField $block "hardness" "1.0") (Get-QuotedField $block "required_tool_tag") `
        (Get-NumberField $block "required_mining_level" "0") (Get-Drops $block) `
        (Get-NumberField $block "collapse_chance" "0.3") `
        (Get-NumberField $block "support_radius" "5") (Get-QuotedField $block "rock_layer_key")
}

$periodicStart = $interactionSource.IndexOf("var _periodic_ores := [")
$periodicEnd = $interactionSource.IndexOf("for _ore in _periodic_ores:", $periodicStart)
if ($periodicStart -lt 0 -or $periodicEnd -le $periodicStart) {
    throw "Could not locate the legacy periodic ore table."
}
$periodicSource = $interactionSource.Substring($periodicStart, $periodicEnd - $periodicStart)
$periodicPattern = '\{\s*"id"\s*:\s*(MAT_[A-Z0-9_]+),\s*"key"\s*:\s*"([^"]+)",\s*"hardness"\s*:\s*([0-9.]+),\s*"level"\s*:\s*(\d+),\s*"item"\s*:\s*"([^"]+)"\s*\}'
foreach ($match in [regex]::Matches($periodicSource, $periodicPattern)) {
    $symbol = $match.Groups[1].Value
    if (-not $constants.ContainsKey($symbol)) {
        throw "Unknown periodic terrain material symbol '$symbol'."
    }
    $key = $match.Groups[2].Value
    $oreName = $key.Replace("snt:ore_", "")
    $drop = [pscustomobject]@{
        ItemKey = $match.Groups[5].Value
        Count = "1"
        MinCount = "1"
        MaxCount = "1"
        Chance = "1.0"
    }
    Add-MaterialRecord $materials $constants[$symbol] $key ("terrain.{0}_ore" -f $oreName) `
        "TF_SOLID | TF_MINEABLE" $match.Groups[3].Value "pickaxe" $match.Groups[4].Value @($drop)
}

$cropStart = $interactionSource.IndexOf("var _crop_species_mats := [")
$cropEnd = $interactionSource.IndexOf("for sp in _crop_species_mats:", $cropStart)
if ($cropStart -lt 0 -or $cropEnd -le $cropStart) {
    throw "Could not locate the legacy crop stage material table."
}
$cropSource = $interactionSource.Substring($cropStart, $cropEnd - $cropStart)
$cropPattern = '\{\s*"name"\s*:\s*"([^"]+)",\s*"base_id"\s*:\s*(MAT_[A-Z0-9_]+),\s*"seed_key"\s*:\s*"([^"]+)",\s*"crop_key"\s*:\s*"([^"]+)"\s*\}'
$cropStages = @("seed", "sprout", "growing", "mature")
foreach ($match in [regex]::Matches($cropSource, $cropPattern)) {
    $symbol = $match.Groups[2].Value
    if (-not $constants.ContainsKey($symbol)) {
        throw "Unknown crop terrain material symbol '$symbol'."
    }
    $baseId = $constants[$symbol]
    $name = $match.Groups[1].Value
    $seedKey = $match.Groups[3].Value
    $cropKey = $match.Groups[4].Value
    for ($index = 0; $index -lt $cropStages.Count; ++$index) {
        $drops = [System.Collections.Generic.List[object]]::new()
        if ($index -lt 3) {
            $drops.Add([pscustomobject]@{ ItemKey = $seedKey; Count = "1"; MinCount = "1"; MaxCount = "1"; Chance = "1.0" })
        } else {
            $drops.Add([pscustomobject]@{ ItemKey = $cropKey; Count = "1"; MinCount = "1"; MaxCount = "1"; Chance = "1.0" })
            $drops.Add([pscustomobject]@{ ItemKey = $seedKey; Count = "1"; MinCount = "1"; MaxCount = "1"; Chance = "1.0" })
        }
        $stage = $cropStages[$index]
        Add-MaterialRecord $materials ($baseId + $index) ("snt:{0}_{1}" -f $name, $stage) `
            ("terrain.{0}_{1}" -f $name, $stage) "TF_WALKABLE | TF_MINEABLE" "0.0" "" "0" @($drops)
    }
}

$orderedMaterials = @($materials.Values | Sort-Object Id)
if ($orderedMaterials.Count -ne 152 -or $orderedMaterials[0].Id -ne 0 -or $orderedMaterials[-1].Id -ne 151) {
    throw "Expected 152 legacy terrain materials with ids 0..151; found $($orderedMaterials.Count)."
}
$orderedMaterialsForOutput = @($orderedMaterials | Sort-Object Key)
for ($expectedId = 0; $expectedId -lt 152; ++$expectedId) {
    if ($orderedMaterials[$expectedId].Id -ne $expectedId) {
        throw "Legacy terrain material id $expectedId is missing."
    }
}

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("// Generated by tools/migrate_legacy_terrain_material_catalog.ps1 from the retired Godot catalog.")
$lines.Add("// This file is game-owned static content; it is not loaded through Godot at runtime.")
$lines.Add("")
$lines.Add("#define SNT_LOG_CHANNEL `"game.worldgen_catalog`"")
$lines.Add("#include `"game/worldgen/legacy_terrain_material_catalog.h`"")
$lines.Add("")
$lines.Add("#include `"core/log.h`"")
$lines.Add("#include `"game/worldgen/world_gen_config.h`"")
$lines.Add("")
$lines.Add("#include <utility>")
$lines.Add("")
$lines.Add("namespace snt::game {")
$lines.Add("namespace {")
$lines.Add("")
$lines.Add("void add_material(WorldGenConfigSnapshot& config, TerrainMaterialDef material) {")
$lines.Add("    config.materials.push_back(std::move(material));")
$lines.Add("}")
$lines.Add("")
$lines.Add("}  // namespace")
$lines.Add("")
$lines.Add("void register_migrated_legacy_terrain_material_catalog(WorldGenConfigSnapshot& config) {")
$lines.Add("    config.materials.reserve(config.materials.size() + 152);")
foreach ($material in $orderedMaterialsForOutput) {
    $lines.Add("    add_material(config, {")
    $lines.Add(("        .key = `"{0}`"," -f (Escape-CppString $material.Key)))
    $lines.Add(("        .title_key = `"{0}`"," -f (Escape-CppString $material.TitleKey)))
    $lines.Add(("        .flags = {0}," -f $material.Flags))
    $lines.Add(("        .hardness = {0}f," -f $material.Hardness))
    if (-not [string]::IsNullOrEmpty($material.RequiredToolTag)) {
        $lines.Add(("        .required_tool_tag = `"{0}`"," -f (Escape-CppString $material.RequiredToolTag)))
    }
    $lines.Add(("        .required_mining_level = {0}," -f $material.RequiredMiningLevel))
    if ($material.Drops.Count -gt 0) {
        $lines.Add("        .drops = {")
        foreach ($drop in $material.Drops) {
            $lines.Add(("            {{.item_key = `"{0}`", .count = {1}, .min_count = {2}, .max_count = {3}, .chance = {4}f}}," -f (Escape-CppString $drop.ItemKey), $drop.Count, $drop.MinCount, $drop.MaxCount, $drop.Chance))
        }
        $lines.Add("        },")
    }
    if ($material.CollapseChance -ne "0.3") {
        $lines.Add(("        .collapse_chance = {0}f," -f $material.CollapseChance))
    }
    if ($material.SupportRadius -ne "5") {
        $lines.Add(("        .support_radius = {0}," -f $material.SupportRadius))
    }
    if (-not [string]::IsNullOrEmpty($material.RockLayerKey)) {
        $lines.Add(("        .rock_layer_key = `"{0}`"," -f (Escape-CppString $material.RockLayerKey)))
    }
    $lines.Add("    });")
}
$lines.Add("")
$lines.Add("    SNT_LOG_INFO(`"Registered migrated terrain material catalog: %zu semantic definitions`",")
$lines.Add("                 config.materials.size());")
$lines.Add("}")
$lines.Add("")
$lines.Add("}  // namespace snt::game")

$outputDirectory = Split-Path -Parent $OutputPath
if (-not [string]::IsNullOrEmpty($outputDirectory)) {
    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}
[System.IO.File]::WriteAllLines($OutputPath, $lines, [System.Text.UTF8Encoding]::new($false))
Write-Host "Migrated $($orderedMaterials.Count) legacy terrain materials to $OutputPath"
