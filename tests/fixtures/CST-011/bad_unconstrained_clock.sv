// sverity-args: --top top --sdc @DIR@/only_a.sdc
// clkb has no create_clock.
// expect: CST-011 WARNING
module top(input logic clka, clkb, rst_n, d, output logic q);
  logic a_q, b_q;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 1'b0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) b_q <= 1'b0; else b_q <= a_q;
  assign q = b_q;
endmodule
