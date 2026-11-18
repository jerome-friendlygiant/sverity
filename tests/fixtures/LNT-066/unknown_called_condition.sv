// sverity-args: --top top
// expect: LNT-066 WARNING 8
// Opaque condition dependency must be Unresolved.
module top(input logic clk, reset_n, d, output logic y);
 logic q;
 always_ff @(posedge clk or negedge reset_n) if (!reset_n) q <= 'x; else q <= d;
 import "DPI-C" function logic foreign_condition(input logic c);
 assign y = foreign_condition(q);
endmodule
