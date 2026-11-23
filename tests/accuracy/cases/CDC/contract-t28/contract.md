# CDC-RFC1-T28 interface contract

`div` is a 20 ns input clock and `src_clk` is a 10 ns input clock. SDC declares `src_clk` asynchronous to `dst_clk` but leaves `div` unrelated to both. The `sampled` output is a direct `src_clk` capture of state clocked by `div`; the relationship is intentionally unknown. `rst_n` asynchronously asserts both state elements; recovery/removal and release behavior are not established.
