// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: s2 <= s1 & ~s1; => s2 <= s1;
// revert-expect-none: CDC-006 ERROR
// expect: CDC-006 ERROR
module top(input logic clka, clkb, rst_n, d, en, output logic q);
  logic a_q, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 1'b0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) s1 <= 1'b0; else s1 <= a_q;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) s2 <= 1'b0; else s2 <= s1 & ~s1;
  assign q = s2;
endmodule
