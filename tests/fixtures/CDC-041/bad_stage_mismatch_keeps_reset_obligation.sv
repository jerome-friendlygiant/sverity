// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-041 WARNING
// expect: CDC-026 ERROR
// expect-none: CDC-003 INFO
module top(input logic clka, clkb, rst_n, rst2_n, por_n, d, output logic q);
  logic a_q, s1;

  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) s1 <= 0; else s1 <= a_q;
  always_ff @(posedge clkb or negedge rst2_n) if (!rst2_n) q <= 0; else q <= s1;
endmodule
