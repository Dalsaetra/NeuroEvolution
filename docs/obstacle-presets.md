# Frontier obstacle presets

New nursery-frontier runs use `mixed` obstacles. Select a preset independently of the food distribution:

```powershell
.\scripts\ecosystem.ps1 -Build -ObstaclePreset mixed
.\scripts\ecosystem.ps1 -ObstaclePreset valleys -ObstacleScale 32
.\scripts\ecosystem.ps1 -ObstaclePreset rooms -ObstacleDensity 0.08
.\scripts\ecosystem.ps1 -ObstaclePreset labyrinth
.\scripts\ecosystem.ps1 -ObstaclePreset sparse
```

| Preset | Layout |
|---|---|
| `sparse` | The previous generator: separated 3–7-cell wall lines, sometimes bent; approximately 2% wall coverage. |
| `valleys` | Pairs of long, gently winding ridges, with open ends and broad corridors between them. |
| `rooms` | House-like outer walls, two exterior doorways, and internal partitions with doors. |
| `labyrinth` | Small mazes made by recursively dividing rooms, with doorways between sections and four outer gates. |
| `mixed` | A seeded mixture of valleys, rooms, and labyrinth clusters. |

These are physical walls, not buildings with roofs: room interiors do not confer storm protection. Existing shelter tiles keep that role. Food clearings can interrupt a structure, so clusters near trees and fields look more like ruins than complete houses.

## Tuning

Defaults live in `EcosystemConfig` in [config.hpp](../include/neuroevo/config.hpp):

| Field | Default | Executable flag | Meaning |
|---|---:|---|---|
| `obstacle_preset` | `ObstaclePreset::Mixed` | `--obstacle-preset` | Which frontier wall generator to use. |
| `obstacle_density` | `0.06` | `--obstacle-density` | Maximum fraction of eligible frontier cells painted as walls, from 0 to 0.15. |
| `obstacle_scale` | `24` | `--obstacle-scale` | Approximate cluster span in cells, from 12 to 64. |

Density and scale apply to the four structured presets. `sparse` retains its historical fixed density and wall lengths. Set density to zero in a structured preset to remove generated frontier obstacles while retaining the nursery and world boundary walls.

Density is a ceiling, not a quota: food, shelters, connectivity checks, limited placement attempts, and spacing may keep actual coverage below it. Larger scales favor longer ridges and larger buildings/mazes, but may fit fewer structures, especially on small maps. Each structure varies around the configured scale. At most half the candidate frontier area is assigned to structure footprints; gaps between clusters and food clearings preserve open ground. Structures have three-cell doorways; valley ridges are at least five cells apart.

## Access, determinism, and saves

- Nursery walls, gates, and the three-cell approach zone keep their existing layout.
- Structured walls avoid food-source reservations with an extra 1.5-cell margin, shelter floors, and their immediate entrances.
- Each proposed cluster is checked against the whole outdoor floor. Placements that disconnect any open cell are rejected, including when the nursery gates are intentionally closed.
- Small fragments left by clipping around protected areas are discarded.
- A separate seeded generator makes obstacle placement deterministic and independent of founder population. The existing rough-ground pass runs after walls are finalized.

Checkpoint version 47 stores the preset, density, and scale alongside the saved terrain. Resume preserves the existing map and rejects obstacle overrides. Older checkpoints load as `sparse` without regenerating terrain. Importing starting genomes into a fresh run uses that new run's obstacle settings. Replay metadata and `summary.json` record the selected preset and tuning. The generated and ancestor-nursery control habitats retain their existing obstacle rules; the CLI rejects explicit obstacle presets for those habitats.
