# rfc1-h41 contract

## Summary

A request pulse arrives on a primary input from an upstream block clocked by src_clk (the upstream clock port is provided for constraints) and is sampled by dst_clk (10 ns). The interface description guarantees a minimum assertion of 12 ns and every pulse must be seen.

## Clocks

- `src_clk`: period 20.0 ns, top-level port `src_clk`
- `dst_clk`: period 10.0 ns, top-level port `dst_clk`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).
- `events.json`: interface contract for primary input `req_pulse_i` (launch clock `src_clk`, protocol `event`, minimum assertion 12 ns, delivery `required`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.
