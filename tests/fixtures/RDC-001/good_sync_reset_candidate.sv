// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A synchronous reset (constants assigned under a top-level if on one signal) is classified with its polarity, heuristically.
// expect: RDC-001 INFO
module top(input logic clka, srst_n, d, output logic q);
  always_ff @(posedge clka) if (!srst_n) q <= 1'b0; else q <= d;
endmodule
