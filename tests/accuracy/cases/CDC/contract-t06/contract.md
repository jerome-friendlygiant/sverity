# CDC-RFC1-T06 interface contract

Source holds payload stable from request assertion through synchronized acknowledgment (which this IP omits). Capture must be qualified by destination-domain synchronized request, not raw src_req.

Clock periods in this case are `src_clk=10 ns`, `dst_clk=14 ns`; clocks are asynchronous by SDC. `rst_n` is asynchronous active-low assertion. No physical metastability, skew, recovery/removal, or clock-tree claim is established by this ideal RTL/SDC model.
