// sverity-args: --top top --sdc @DIR@/fpclk.sdc
// expect: CDC-038 WARNING
// Regression from the audit: get_clocks based exceptions must be recognised.
module top(input logic clka, clkb, rst_n, d, output logic q);
  logic a_q;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 1'b0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= a_q;
endmodule
