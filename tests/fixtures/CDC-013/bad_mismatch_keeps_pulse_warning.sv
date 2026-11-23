// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-013 WARNING
// expect: CDC-026 ERROR
module top(input logic clka, clkb, rst_n, rst2_n, d, output logic q, p);
  logic a_q, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) s1 <= 0; else s1 <= a_q;
  always_ff @(posedge clkb or negedge rst2_n) if (!rst2_n) s2 <= 0; else s2 <= s1;
  assign p = s1;
  assign q = s2;
endmodule
