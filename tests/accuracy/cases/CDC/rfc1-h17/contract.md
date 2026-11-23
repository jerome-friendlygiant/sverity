# rfc1-h17 contract

## Summary

A capture register runs on a gated version of clk_b. The enable is registered on clk_b.

## Clocks

- `clk_b`: period 8.0 ns, top-level port `clk_b`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.
