# rfc1-h27 contract

## Summary

An optional status pulse from an upstream block clocked by src_clk (the upstream clock port is provided for constraints) is recorded in a sticky flag in dst_clk. The interface description says the pulse is advisory and may be missed.

## Clocks

- `src_clk`: period 20.0 ns, top-level port `src_clk`
- `dst_clk`: period 10.0 ns, top-level port `dst_clk`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).
- `events.json`: interface contract for primary input `pulse_i` (launch clock `src_clk`, protocol `event`, minimum assertion 4 ns, delivery `best_effort`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.
