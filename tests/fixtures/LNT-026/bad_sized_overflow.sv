// sverity-args: --top top
// revert: 3'd9 => 3'd7
// revert: 4'hFF => 4'hF
// revert-expect-none: LNT-026
// expect: LNT-026 WARNING 9
// expect: LNT-026 WARNING 10
// expect-count: LNT-026 WARNING 2
module top(input logic clk, output logic [2:0] o3, output logic [3:0] o4);
  assign o3 = 3'd9;      // 4 bits needed, 3 available: value truncated
  assign o4 = 4'hFF;     // 8 bits needed, 4 available
endmodule
