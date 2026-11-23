# CDC-RFC1-T25 interface contract

mode_a is sampled only while both clocks are stopped and is immutable for the entire operation mode. No dynamic switch is allowed. RTL mux is not intrinsically glitch-free; static exclusivity must be declared and externally enforced.

`clk_a` is 10 ns and `clk_b` is 14 ns; SDC declares them exclusive. `mode_a` is fixed while either clock toggles and may change only when both clock sources are stopped. This is a static-mode contract; it does not make the RTL mux safe for dynamic switching. `rst_n` is unused in this IP.
