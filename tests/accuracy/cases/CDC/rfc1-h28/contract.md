# rfc1-h28 contract

## Summary

A mode level from an upstream block clocked by src_clk (the upstream clock port is provided for constraints) is sampled in dst_clk and reported. The interface description guarantees the level is held at least 20 ns and every change must be seen.

## Clocks

- `src_clk`: period 40.0 ns, top-level port `src_clk`
- `dst_clk`: period 10.0 ns, top-level port `dst_clk`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).
- `events.json`: interface contract for primary input `level_i` (launch clock `src_clk`, protocol `level`, minimum assertion 20 ns, delivery `required`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.
