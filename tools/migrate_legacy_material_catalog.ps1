param(
    [string]$Source = "scripts/material/MaterialDefinitions.gd",
    [string]$Destination = "game/scripts/00_material_catalog.as"
)

$ErrorActionPreference = "Stop"
$raw = [System.IO.File]::ReadAllText((Resolve-Path -LiteralPath $Source))
$startMarker = "const _ALL_MATERIALS := ["
$endMarker = "# Mineral compound item keys"
$start = $raw.IndexOf($startMarker, [System.StringComparison]::Ordinal)
$end = $raw.IndexOf($endMarker, [System.StringComparison]::Ordinal)
if ($start -lt 0 -or $end -le $start) {
    throw "Could not locate the legacy material catalog."
}

$catalog = $raw.Substring($start + $startMarker.Length, $end - $start - $startMarker.Length)
$catalog = "[" + $catalog.Substring(0, $catalog.LastIndexOf("]")) + "]"
$catalog = [regex]::Replace($catalog, "(?m)^\s*#.*(?:\r?\n|$)", "")

$constants = [ordered]@{
    "GAS_PLASMA" = 48
    "FLUID_PLASMA" = 48
    "METAL_FULL" = 195
    "ORE_FULL" = 203
    "GEM_FULL" = 133
    "DUST_ONLY" = 1
    "GEN_PLASMA" = 32
    "GEN_METAL" = 2
    "GEN_BLOCK" = 128
    "GEN_PLATE" = 256
    "GEN_DUST" = 1
    "GEN_GEM" = 4
    "GEN_ORE" = 8
    "GEN_CELL" = 16
    "GEN_WIRE" = 64
    "GEN_ROD" = 512
    "FLUID" = 16
    "GAS" = 16
    "SOLID" = 0
    "LIQUID" = 1
    "GASEOUS" = 2
    "PLASMA" = 3
}
foreach ($name in ($constants.Keys | Sort-Object { $_.Length } -Descending)) {
    $catalog = [regex]::Replace(
        $catalog,
        "\b" + [regex]::Escape($name) + "\b",
        [string]$constants[$name])
}

$catalog = [regex]::Replace($catalog, "0x([0-9A-Fa-f]+)", {
    param($match)
    return [Convert]::ToInt32($match.Groups[1].Value, 16).ToString(
        [System.Globalization.CultureInfo]::InvariantCulture)
})
$catalog = [regex]::Replace($catalog, '"gen_flags"\s*:\s*([^,}]+)', {
    param($match)
    $expression = $match.Groups[1].Value.Trim()
    if ($expression -notmatch '^[0-9\s|]+$') {
        throw "Unsupported generation-flag expression: $expression"
    }
    [int]$flags = 0
    foreach ($part in ($expression -split '\|')) {
        $flags = $flags -bor [int]$part.Trim()
    }
    return '"gen_flags": ' + $flags
})
$catalog = [regex]::Replace($catalog, ",\s*\]", "]")
$materials = $catalog | ConvertFrom-Json

$invariant = [System.Globalization.CultureInfo]::InvariantCulture
function Escape-AngelScriptString([string]$Value) {
    return $Value.Replace("\\", "\\\\").Replace('"', '\"')
}
function Format-Float([double]$Value) {
    return $Value.ToString("0.0###############", $invariant) + "f"
}

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("// Generated from the retired Godot material catalog. This file is game-owned")
$lines.Add("// AngelScript content: material physics and generated form definitions.")
$lines.Add("void snt_register() {")
foreach ($material in $materials) {
    $line = '    snt_register_material("{0}", "{1}", {2}, {3}, {4}, {5}, {6}, {7}, "{8}");' -f `
        (Escape-AngelScriptString $material.name), `
        (Escape-AngelScriptString $material.title_key), `
        ([int]$material.gen_flags), `
        ([int]$material.state), `
        ([int]$material.color), `
        ([int]$material.melting_point), `
        ([int]$material.boiling_point), `
        (Format-Float ([double]$material.mass)), `
        (Escape-AngelScriptString $material.chemical_formula)
    $lines.Add($line)
    foreach ($element in $material.elements) {
        $lines.Add(('    snt_add_material_element("{0}", "{1}", {2});' -f `
            (Escape-AngelScriptString $material.name), `
            (Escape-AngelScriptString $element.element),
            ([int]$element.count)))
    }
}

# Legacy special forms used semantic icons rather than a generic tinted base.
$lines.Add('    snt_set_material_form_presentation("wood", 0, "item.wood_log", 64, "materials/wood_log_icon_32.png", "", 16777215, false);')
$lines.Add('    snt_set_material_form_presentation("wood", 16, "item.wood_plank", 64, "materials/wood_plank_icon_32.png", "", 16777215, false);')
$lines.Add('    snt_set_material_form_presentation("wood", 19, "item.stick", 64, "materials/stick_icon_32.png", "", 16777215, false);')
$lines.Add('    snt_set_material_form_presentation("coal", 8, "item.coal", 64, "material_sets/generic/gem_base_32.png", "", 1710618, true);')
$lines.Add('    snt_log("Registered migrated material physics catalog: ' + $materials.Count + ' materials");')
$lines.Add("}")

$directory = Split-Path -Parent $Destination
New-Item -ItemType Directory -Force -Path $directory | Out-Null
Set-Content -LiteralPath $Destination -Value $lines -Encoding utf8
Write-Output ("Generated {0} material definitions at {1}" -f $materials.Count, $Destination)
