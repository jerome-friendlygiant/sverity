# CDC-RFC1-T20 interface contract

External event remains asserted at least two dst_clk periods and has no second event until the first is acknowledged through a separate system-level mechanism. RTL only retains evidence; no exactly-once proof.

Clock periods in this case are `src_clk=10 ns`, `dst_clk=14 ns`; clocks are asynchronous by SDC. `rst_n` is asynchronous active-low assertion. No physical metastability, skew, recovery/removal, or clock-tree claim is established by this ideal RTL/SDC model.
