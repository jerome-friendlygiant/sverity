// sverity-args: --top top
// expect: LNT-026 WARNING 6
// expect: LNT-026 WARNING 7
// expect-count: LNT-026 WARNING 2
module top(input logic clk, output logic [2:0] o3, output logic [3:0] o4);
  assign o3 = 3'd9;      // 4 bits needed, 3 available: value truncated
  assign o4 = 4'hFF;     // 8 bits needed, 4 available
endmodule
