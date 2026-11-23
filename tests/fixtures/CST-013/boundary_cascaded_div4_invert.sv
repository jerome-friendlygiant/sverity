// sverity-args: --top top --sdc @DIR@/div4_inv.sdc
// Cascaded toggle-flop divide-by-4 with combinational inversion matches -divide_by 4 -invert.
// expect-none: CST-013 WARNING
// expect-none: CST-013 ERROR
// expect: CST-013 INFO
module top(input logic clk_src, d, output logic q);
  logic div2_q, div4_q, div4_n;
  always_ff @(posedge clk_src) div2_q <= ~div2_q;
  always_ff @(posedge div2_q) div4_q <= ~div4_q;
  assign div4_n = ~div4_q;
  always_ff @(posedge div4_n) q <= d;
endmodule
