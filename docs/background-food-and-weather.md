# Background food and gradual storms

Fresh fields-and-trees worlds include 250 scattered low-quality grazing patches
in addition to their persistent fields and trees. Patches can occur on any
traversable frontier floor, including ordinary shelters and open ground. The
nursery retains its separate food quota and nutrition policy. Background patches
are not tied to shelters: a depleted patch may next appear inside or outside one.
Placement avoids other resources by at least 0.8 world units.

Defaults are 10 energy per biomass (compared with field energy 25), capacity 3,
decay 0.005 biomass/second, and a one-second delay after depletion before a full
patch relocates. Initial stocks have randomized ages. This reuses the old
scattered-graze renewal rules, including decay and relocation during storms.
Ordinary shelter food uses the same background-food nutrition as exposed food.
The existing `outdoor_food_relocates`, `graze_capacity`, `graze_decay`,
`graze_regrowth`, and `outdoor_food_respawn_delay` settings govern this lifecycle.

Tune `background_food_patches` and `background_food_energy` in `EcosystemConfig`,
or pass `--background-food-patches N` and `--background-food-energy X`. A count of
zero disables the extra layer. The scattered food preset keeps its existing
resource counts and nutrition; the new layer applies to fields-and-trees worlds.

## Storm intensity

Fresh worlds default to `storm_ramp = true`. During a storm, let `p` be the
fraction of its duration elapsed. Intensity is `I = 1 - abs(2*p - 1)`:

| Fraction elapsed | 0% | 25% | 50% | 75% | 100% |
| --- | ---: | ---: | ---: | ---: | ---: |
| Intensity | 0 | 0.5 | 1 | 0.5 | 0 |
| Exposed harvesting effectiveness | 100% | 75% | 50% | 75% | 100% |

Exposed health damage is `storm_damage * I` per second. The configured damage
is the **peak**, not the average: the default 0.35 over a 60-second storm deals
10.5 health damage to a creature exposed throughout, before other effects.
This is half the integrated damage of the old flat storm with the same setting.
Legacy energy-drain mode also multiplies its exposure cost by intensity.

Harvesting scales the requested ingestion rate by `1 - 0.5*I`, for plants and
meat. Pod-opening effort gets the same multiplier before the existing cooperative
work calculation. Intent and its energy cost are unchanged. Shelter status is
checked after movement: protected creatures take no storm damage and retain full
harvesting effectiveness. Plant regrowth/ripening still pauses during storms under
the existing rules. The neural storm-warning cue is unchanged; replay frames also
record physical `storm_intensity` separately. Effects use the start of each
simulation interval, as the weather system already did.

`--storm-ramp 0` restores flat damage and the full exposed-harvesting cutoff.
Checkpoint format 37 stores the background settings and ramp policy. Formats
22–36 load with no additional background food and their historical flat storm
behavior. New worlds seeded with old genomes use the new world defaults.
