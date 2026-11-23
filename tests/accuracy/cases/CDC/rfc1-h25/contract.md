# rfc1-h25 contract

## Summary

An 8-bit value is registered in a first module and passed to a second module that registers it again. Both modules are driven by clk_a.

## Clocks

- `clk_a`: period 10.0 ns, top-level port `clk_a`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.
