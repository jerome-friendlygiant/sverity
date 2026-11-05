// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Only one asynchronous reset assertion source exists, so multi-source sequencing is not triggered.
// expect-none: RDC-011
module top(input logic clka, clkb, rst_a_n, d, output logic q, r);
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) q <= 1'b0; else q <= d;
  always_ff @(posedge clkb or negedge rst_a_n) if (!rst_a_n) r <= 1'b0; else r <= d;
endmodule
