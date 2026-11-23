# rfc1-h08 contract

## Summary

Two related status flags launched in src_clk are each taken into dst_clk, and a dst_clk register reports when both are set.

## Clocks

- `src_clk`: period 20.0 ns, top-level port `src_clk`
- `dst_clk`: period 8.0 ns, top-level port `dst_clk`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.
