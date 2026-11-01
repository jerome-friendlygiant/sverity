// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Same clock domain: no crossing finding of any kind.
// expect-none: CDC-003
// expect-none: CDC-004
// expect-none: CST-004
module top(input logic clka, clkb, rst_n, d, output logic q);
  logic a_q, b_q;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 1'b0; else a_q <= d;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) b_q <= 1'b0; else b_q <= a_q;
  assign q = b_q;
endmodule
