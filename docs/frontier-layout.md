# Frontier wall layout

New nursery-frontier worlds use short, randomly positioned wall lines instead
of the old regular grid of isolated pillars. Each line has 3–7 edge-connected
wall tiles, one tile thick, and is either straight or has one right-angle bend.
Separate lines cannot touch, including diagonally, preventing larger blobs or
closed rings. The wall budget is 2% of the candidate outer area; placement may
use fewer tiles when space is tight. Nursery approaches and map-edge corridors
remain open.

Shelters are placed first, so lines can cross shelter footprints. Covered tiles
become impassable walls; the remaining shelter floor retains storm protection.
The center food patch and its immediate approaches stay clear. Every proposed
line is checked with an outdoor flood fill and rejected if it would isolate any
walkable outdoor cell. Deliberately closed nursery gates are supported.

Wall layout has a separate seeded RNG and does not depend on creature count.
Outdoor food placement skips walls, maintaining its existing type quotas.
Saved maps retain their exact terrain; start a new nursery-frontier world to
use this layout. The older generated habitat has its existing obstacle recipe.

## Rough ground

Both new map types group rough ground into broad, overlapping elliptical patches
instead of independent single-tile noise. Patch radii range from 3–6 tiles on one
axis and 2–4 on the other, with random orientation. Small fragments below eight
edge-connected tiles are removed after shelter and wall clipping. Coverage stays
approximately 18% of outdoor floor in nursery-frontier maps and 12% in generated
maps. Whole patches may slightly overshoot the target. The separate seeded rough
terrain stream does not change wall, shelter or resource placement.

Rough ground still doubles movement energy cost and does not slow movement.
Creatures have a current sheltered-state input, directional shelter cues and wall
sensing, but no current-ground-type or rough-ground vision input. Their energy
input reflects the extra expense indirectly; sensing it does not guarantee they
learn to associate the loss with rough terrain. Viewer colors are not inputs to
the brain. This layout change does not alter the sensory interface.

Existing checkpoints retain the terrain they recorded. The updated executable is
available in `build-layout`; generate a new map to use the clustered terrain.
