# rfc1-h20 contract

## Summary

Two registered status bits are presented on output ports. The board-level interface description says the external consumer samples both ports with dst_clk.

## Clocks

- `src_clk`: period 10.0 ns, top-level port `src_clk`
- `dst_clk`: period 6.0 ns, top-level port `dst_clk`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.
