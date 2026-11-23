# CDC-RFC1-T02 interface contract

Only sync2 may feed functional logic. early_seen is a debug output state and is intentionally retained as a real consumer of stage one.

Clock periods in this case are `src_clk=10 ns`, `dst_clk=14 ns`; clocks are asynchronous by SDC. `rst_n` is asynchronous active-low assertion. No physical metastability, skew, recovery/removal, or clock-tree claim is established by this ideal RTL/SDC model.
