# rfc1-h21 contract

## Summary

An 8-bit value is passed through two registered stages in separate modules under one system clock. The clock reaches the stages through a local buffered wire. Reset is synchronous.

## Clocks

- `clk_sys`: period 10.0 ns, top-level port `clk_sys`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.
