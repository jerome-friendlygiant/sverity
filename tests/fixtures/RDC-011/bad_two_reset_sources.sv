// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: negedge rst_b_n) if (!rst_b_n) r => negedge rst_a_n) if (!rst_a_n) r
// revert-expect-none: RDC-011 WARNING
// A register of one source samples a register of the other: the sampling is a reset-domain crossing and no exemption applies.
// expect: RDC-011 WARNING
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clka, clkb, rst_a_n, rst_b_n, d, output logic q, r);
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) q <= 1'b0; else q <= d;
  always_ff @(posedge clkb or negedge rst_b_n) if (!rst_b_n) r <= 1'b0; else r <= q;
endmodule
