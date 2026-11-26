// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: else if (!rst_b_n) q <= 1'b0; => 
// revert-expect-none: RDC-011 WARNING
// A register reset by A is also cleared synchronously by the raw B reset: its behaviour depends on B, so the sources interact.
// expect: RDC-011 WARNING
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clka, clkb, rst_a_n, rst_b_n, d, output logic q, r);
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) q <= 1'b0; else if (!rst_b_n) q <= 1'b0; else q <= d;
  always_ff @(posedge clkb or negedge rst_b_n) if (!rst_b_n) r <= 1'b0; else r <= d;
endmodule
