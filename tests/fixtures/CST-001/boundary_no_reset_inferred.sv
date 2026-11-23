// sverity-args: --top top
// Boundary: unreset clocked register is classified as NO_RESET_INFERRED at INFO with no CST-001 WARNING.
// expect: CST-001 INFO
// expect-none: CST-001 WARNING
module top(input logic clk, d, output logic q);
  always_ff @(posedge clk) q <= d;
endmodule
