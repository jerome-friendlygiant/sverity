// sverity-args: --top top
// No call reaches t, so its write builds no hardware: q is never driven.
// expect: LNT-008 WARNING 5
module top(input logic clk, input logic [3:0] a, output logic [3:0] y);
  logic [3:0] q;
  task automatic t(input logic [3:0] v); q <= v; endtask
  assign y = q;
endmodule
