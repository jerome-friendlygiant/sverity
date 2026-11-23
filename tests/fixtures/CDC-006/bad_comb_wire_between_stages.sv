// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: assign mid = s1 & en; => assign mid = s1;
// revert-expect-none: CDC-006 ERROR
// expect: CDC-006 ERROR
// expect-confidence: CDC-006 established
// expect-none: CDC-007 ERROR
module top(input logic clka, clkb, rst_n, d, en, output logic q);
  logic a_q, s1, s2, mid;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 1'b0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) s1 <= 1'b0; else s1 <= a_q;
  assign mid = s1 & en;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) s2 <= 1'b0; else s2 <= mid;
  assign q = s2;
endmodule
