// sverity-args: --top top
// A latch clock port and an alias of the same source clock identify the low phase through hierarchy.
// expect: CDC-045 INFO
// expect-none: CDC-045 WARNING
module icg(input logic clk, en, output logic gclk);
  logic held;
  always_latch if (!clk) held = en;
  assign gclk = clk & held;
endmodule
module top(input logic clka, en, d, output logic q);
  wire clk_alias = clka;
  wire g;
  icg u(.clk(clk_alias), .en(en), .gclk(g));
  always_ff @(posedge g) q <= d;
endmodule
