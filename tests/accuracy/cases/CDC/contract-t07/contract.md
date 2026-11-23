# CDC-RFC1-T07 interface contract

Source payload is valid only while src_req is high and until ack; destination samples after the synchronized request falling edge, outside the stability window.

Clock periods in this case are `src_clk=10 ns`, `dst_clk=14 ns`; clocks are asynchronous by SDC. `rst_n` is asynchronous active-low assertion. No physical metastability, skew, recovery/removal, or clock-tree claim is established by this ideal RTL/SDC model.
