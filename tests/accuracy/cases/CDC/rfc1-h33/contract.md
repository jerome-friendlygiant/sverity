# rfc1-h33 contract

## Summary

A small arithmetic block is wrapped by input and output registers. All ports are timed against the single interface clock clk_io.

## Clocks

- `clk_io`: period 10.0 ns, top-level port `clk_io`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.
