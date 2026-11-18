// sverity-args: --top top
// slang has not unrolled a loop with a run-time bound; the write may run, so the member is not "never driven".
// expect: LNT-041 WARNING 5
module top(input c, input [3:0] n, output y);
  reg g [2];
  always @(c, n) for (integer i = 0; i < n; i = i + 1) begin
    if (i == 4) break;
    g[0] = c;
  end
  assign y = g[0];
endmodule
