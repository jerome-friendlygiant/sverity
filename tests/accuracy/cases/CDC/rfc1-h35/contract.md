# rfc1-h35 contract

## Summary

A registered clk_a bit is taken into clk_b through a two-register synchronizer module and registered for output. The two clocks come from separate board oscillators.

## Clocks

- `clk_a`: period 20.0 ns, top-level port `clk_a`
- `clk_b`: period 8.0 ns, top-level port `clk_b`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.
