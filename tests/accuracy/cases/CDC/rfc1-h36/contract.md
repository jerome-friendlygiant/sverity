# rfc1-h36 contract

## Summary

A registered clk_src flag is taken into clk_dst through a two-register synchronizer and registered for output. The clk_dst source latency is described in the constraints for timing closure.

## Clocks

- `clk_src`: period 20.0 ns, top-level port `clk_src`
- `clk_dst`: period 8.0 ns, top-level port `clk_dst`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.
