// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Two independent asynchronous reset sources, but no register is controlled by both, none samples a register of the other, and each
// output depends on one of them: no path depends on their order, so the sequencing is a stated trust model and not an obligation.
// expect: RDC-011 INFO
// expect-none: RDC-011 WARNING
module top(input logic clka, clkb, rst_a_n, rst_b_n, d, output logic q, r);
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) q <= 1'b0; else q <= d;
  always_ff @(posedge clkb or negedge rst_b_n) if (!rst_b_n) r <= 1'b0; else r <= d;
endmodule
