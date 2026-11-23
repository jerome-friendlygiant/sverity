// sverity-args: --top top --disable-rule LNT-075
// Boundary: disabling a valid catalog rule ID is accounted for in coverage without a CST-019 unknown-rule warning.
// expect-none: CST-019
module top(input logic clk, d, output logic q);
  always_ff @(posedge clk) q <= d;
endmodule
