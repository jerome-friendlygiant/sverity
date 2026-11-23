# CDC-RFC1-T08 interface contract

Each accepted event toggles src_toggle and source guarantees toggle separation of at least four dst_clk periods. Destination must emit one-cycle pulse per transition; it currently exposes level.

Clock periods in this case are `src_clk=10 ns`, `dst_clk=14 ns`; clocks are asynchronous by SDC. `rst_n` is asynchronous active-low assertion. No physical metastability, skew, recovery/removal, or clock-tree claim is established by this ideal RTL/SDC model.
