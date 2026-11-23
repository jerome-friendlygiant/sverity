# rfc1-h11 contract

## Summary

An 8-bit payload and a valid flag are registered in clk_a. The valid flag is synchronized into clk_b and enables capture of the payload. A diagnostic input in clk_b can also force a capture.

## Clocks

- `clk_a`: period 10.0 ns, top-level port `clk_a`
- `clk_b`: period 8.0 ns, top-level port `clk_b`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.
