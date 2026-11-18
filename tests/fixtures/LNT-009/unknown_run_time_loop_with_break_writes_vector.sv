// sverity-args: --top top
// slang has not unrolled a loop with a run-time bound, so its (missing) drivers say nothing of what the break skips: the write
// may reach g[0] and overlap the second process.
// expect: LNT-009 WARNING 11
module top(input c, input [3:0] n, output [7:0] y);
  reg [7:0] g;
  always @(c, n) for (integer i = 0; i < n; i = i + 1) begin
    if (i == 4) break;
    g[i] = c;
  end
  always @(c) g[0] = 0;
  assign y = g;
endmodule
