# CDC-RFC1-T04 interface contract

src_a and src_b are mutually exclusive indications from the same source transaction. Destination decision depends on their relative arrival, which is not preserved by independent synchronizers.

Clock periods in this case are `src_clk=10 ns`, `dst_clk=14 ns`; clocks are asynchronous by SDC. `rst_n` is asynchronous active-low assertion. No physical metastability, skew, recovery/removal, or clock-tree claim is established by this ideal RTL/SDC model.
