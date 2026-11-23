# rfc1-h23 contract

## Summary

A slow-domain error input is registered and reported in the fast domain on a registered output.

## Clocks

- `src_clk`: period 20.0 ns, top-level port `src_clk`
- `dst_clk`: period 8.0 ns, top-level port `dst_clk`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.
