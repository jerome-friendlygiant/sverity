// sverity-args: --top top
// slang has not unrolled a loop with a run-time bound; the write may run, so the output is not "never driven".
// expect: LNT-024 WARNING 5
module top(input c, input [3:0] n,
           output reg y);
  always @(c, n) for (integer i = 0; i < n; i = i + 1) begin
    if (i == 4) break;
    y = c;
  end
endmodule
