// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// sw_rst is known active-high (it is an asynchronous set source of p), but it is combined into an active-low reset: the polarity premise is contradicted, so the assertion condition is Unresolved.
// expect-unresolved: RDC-008
module top(input logic clka, rst_n, sw_rst, d, output logic p, q);
  logic r;
  always_ff @(posedge clka or posedge sw_rst) if (sw_rst) p <= 1'b0; else p <= d;
  assign r = rst_n & sw_rst;
  always_ff @(posedge clka or negedge r) if (!r) q <= 1'b0; else q <= d;
endmodule
