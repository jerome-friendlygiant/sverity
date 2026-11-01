// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// a reset built from a single source is not a combination
// expect-none: RDC-008
module top(input logic clka, rst_a_n, d, output logic q);
  logic r;
  assign r = rst_a_n;
  always_ff @(posedge clka or negedge r) if (!r) q <= 1'b0; else q <= d;
endmodule
