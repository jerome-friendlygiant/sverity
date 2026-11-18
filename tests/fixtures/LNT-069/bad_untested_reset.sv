// sverity-args: --top top
// revert: or negedge rst_n) q <= a; => or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= a;
// revert-expect-none: LNT-069
// expect: LNT-069 ERROR 6
module top(input logic clk, rst_n, a, output logic q);
  always_ff @(posedge clk or negedge rst_n) q <= a;
endmodule
