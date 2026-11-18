// sverity-args: --top top
// expect: LNT-036 WARNING 5
// expect-count: LNT-036 WARNING 1
module top(input logic [2:0] level, output logic [7:0] mask);
  assign mask = 8'd1 << (level + 3'd1); // 7 + 1 wraps to zero before it becomes a shift amount
endmodule
