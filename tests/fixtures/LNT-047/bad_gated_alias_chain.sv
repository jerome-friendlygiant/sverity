// sverity-args: --top top
// expect: LNT-047 WARNING 6
// revert: clk & en => clk
// revert-expect-none: LNT-047
module top(input logic clk,en,d, output logic q);
logic gated,alias_clk; assign gated = clk & en; assign alias_clk = gated; always @(posedge alias_clk) q <= d;
endmodule
