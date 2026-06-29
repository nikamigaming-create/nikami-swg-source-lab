[CmdletBinding()]
param(
    [string]$PlayerOid = "",
    [string]$InventoryOid = "",
    [string]$CreatureOid = "",
    [string]$OutFile = "",
    [switch]$IncludePersistHints
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..\..")
$proofRoot = Join-Path $repoRoot "proofs"
if (!(Test-Path -LiteralPath $proofRoot)) {
    New-Item -ItemType Directory -Path $proofRoot | Out-Null
}

if ([string]::IsNullOrWhiteSpace($OutFile)) {
    $OutFile = Join-Path $proofRoot "vr-test-weapon-loadout.commands.txt"
}

$inventoryTarget = if ([string]::IsNullOrWhiteSpace($InventoryOid)) { "<INVENTORY_OID>" } else { $InventoryOid }
$creatureTarget = if ([string]::IsNullOrWhiteSpace($CreatureOid)) { "<CREATURE_OID>" } else { $CreatureOid }
$playerTarget = if ([string]::IsNullOrWhiteSpace($PlayerOid)) { "<PLAYER_CREATURE_OID>" } else { $PlayerOid }

$weapons = @(
    @{
        Label = "one-hand lightsaber"
        Server = "object/weapon/melee/sword/sword_lightsaber_vader.iff"
        Client = "object/weapon/melee/sword/shared_sword_lightsaber_vader.iff"
    },
    @{
        Label = "two-hand lightsaber"
        Server = "object/weapon/melee/2h_sword/crafted_saber/sword_lightsaber_two_handed_gen4.iff"
        Client = "object/weapon/melee/2h_sword/crafted_saber/shared_sword_lightsaber_two_handed_gen4.iff"
    },
    @{
        Label = "polearm lightsaber"
        Server = "object/weapon/melee/polearm/crafted_saber/sword_lightsaber_polearm_gen4.iff"
        Client = "object/weapon/melee/polearm/crafted_saber/shared_sword_lightsaber_polearm_gen4.iff"
    },
    @{
        Label = "basic sword"
        Server = "object/weapon/melee/sword/sword_01.iff"
        Client = "object/weapon/melee/sword/shared_sword_01.iff"
    },
    @{
        Label = "ryyk blade"
        Server = "object/weapon/melee/sword/sword_blade_ryyk.iff"
        Client = "object/weapon/melee/sword/shared_sword_blade_ryyk.iff"
    },
    @{
        Label = "wood staff"
        Server = "object/weapon/melee/polearm/lance_staff_wood_s1.iff"
        Client = "object/weapon/melee/polearm/shared_lance_staff_wood_s1.iff"
    },
    @{
        Label = "vibroaxe"
        Server = "object/weapon/melee/axe/axe_vibroaxe.iff"
        Client = "object/weapon/melee/axe/shared_axe_vibroaxe.iff"
    },
    @{
        Label = "vibroblade"
        Server = "object/weapon/melee/knife/knife_vibroblade.iff"
        Client = "object/weapon/melee/knife/shared_knife_vibroblade.iff"
    },
    @{
        Label = "DL-44 pistol"
        Server = "object/weapon/ranged/pistol/pistol_dl44.iff"
        Client = "object/weapon/ranged/pistol/shared_pistol_dl44.iff"
    },
    @{
        Label = "E-11 carbine"
        Server = "object/weapon/ranged/carbine/carbine_e11.iff"
        Client = "object/weapon/ranged/carbine/shared_carbine_e11.iff"
    },
    @{
        Label = "E-11 rifle"
        Server = "object/weapon/ranged/rifle/rifle_e11.iff"
        Client = "object/weapon/ranged/rifle/shared_rifle_e11.iff"
    },
    @{
        Label = "T21 rifle"
        Server = "object/weapon/ranged/rifle/rifle_t21.iff"
        Client = "object/weapon/ranged/rifle/shared_rifle_t21.iff"
    },
    @{
        Label = "rocket launcher"
        Server = "object/weapon/ranged/heavy/heavy_rocket_launcher.iff"
        Client = "object/weapon/ranged/heavy/shared_heavy_rocket_launcher.iff"
    },
    @{
        Label = "acid beam heavy"
        Server = "object/weapon/ranged/heavy/heavy_acid_beam.iff"
        Client = "object/weapon/ranged/heavy/shared_heavy_acid_beam.iff"
    }
)

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("# VR test weapon loadout")
$lines.Add("# Use only on a local/private dev server or client-side visual test.")
$lines.Add("")
$lines.Add("# Server-side real inventory path:")
$lines.Add("# 1. If you do not know the inventory id, run this first:")
$lines.Add("object getInventoryId $playerTarget")
$lines.Add("# 2. Paste the returned inventory id into the createIn commands below.")
$lines.Add("# 3. If AdminPersistAllCreates is off, run object persist <returned weapon NetworkId> for each created weapon.")
$lines.Add("")

foreach ($weapon in $weapons) {
    $lines.Add("# $($weapon.Label)")
    $lines.Add("object createIn $($weapon.Server) $inventoryTarget")
    if ($IncludePersistHints) {
        $lines.Add("# object persist <NetworkId returned by previous createIn>")
    }
}

$lines.Add("")
$lines.Add("# Optional certification/skill inspection before equipping.")
$lines.Add("skill getSkillList $creatureTarget")
$lines.Add("skill getSkillMods $creatureTarget")
$lines.Add("")
$lines.Add("# Client-side visual-only fallback path:")
$lines.Add("# Run one at a time in the client scene parser if you only need VR hand/attachment visuals.")
$lines.Add("scene unequipAll")
foreach ($weapon in $weapons) {
    $lines.Add("# $($weapon.Label)")
    $lines.Add("scene equip $($weapon.Client)")
}

Set-Content -LiteralPath $OutFile -Value $lines -Encoding ASCII
Write-Output "Wrote $OutFile"
Write-Output "Inventory target: $inventoryTarget"
Write-Output "Weapons: $($weapons.Count)"
