# Grunt packets

Send one prompt file at a time. Do not send this whole directory as one chat.

The full assemblies are in `asm/`. The prompts already contain the loops that matter. The other model does not need the full files unless a prompt names one.

| file | send to | what it may change |
|---|---|---|
| `prompt-01-direct-loop.md` | other model | a register map and a replacement for the direct loop only |
| `prompt-02-checked.md` | other model, after 01 comes back | the checked loop only |
| `spec-pages.md` | nobody, unless someone tries to "optimize" placement | nothing. Already in USE.md |

Do not send a prompt that says "make Toplethos faster" with the whole compiler attached.

## What you do

1. Pull `USE.md` and read the Pages section. One decision is still yours: the spare tail of a 3 KiB object may hold a later smaller object. If you want that tail left empty, say so. The rest of the table is the rule you already stated.
2. Send `prompt-01-direct-loop.md` first. When a patch comes back, do not merge it yourself. Hand it here.
3. After a patch is in the tree, run `sh aot/battery.sh` and paste the Toplethos block. That is the check that the sums stayed `509999104.000473380` and that `direct` moved.

## What is not a packet

Page placement will not move the battery. `Poly` is a few cells. The 8.5× gap on `direct` is registers, not pages.

The interpreter is not in these packets.
