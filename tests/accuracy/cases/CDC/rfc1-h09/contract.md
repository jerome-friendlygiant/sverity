# rfc1-h09 contract

## Summary

A four-bit control bundle (all bits are updated together by the source) is taken into dst_clk and presented as one output word.

## Clocks

- `src_clk`: period 10.0 ns, top-level port `src_clk`
- `dst_clk`: period 8.0 ns, top-level port `dst_clk`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.
