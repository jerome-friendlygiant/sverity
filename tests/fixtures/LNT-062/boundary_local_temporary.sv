// sverity-args: --top top
// expect-none: LNT-062
module top(input logic clk,d,en, output logic q);
always_latch if (en) q <= d;
endmodule
