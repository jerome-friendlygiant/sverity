# CDC-RFC1-T22 interface contract

Gray pointer advances by at most one count per src_clk. Physical skew constraint and Gray bus max-delay must be supplied by implementation; RTL alone cannot prove it.

Clock periods in this case are `src_clk=10 ns`, `dst_clk=14 ns`; clocks are asynchronous by SDC. `rst_n` is asynchronous active-low assertion. No physical metastability, skew, recovery/removal, or clock-tree claim is established by this ideal RTL/SDC model.
