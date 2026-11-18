// sverity-args: --top top
// The outer loop is unrolled and left by a break, but the inner loop with a run-time bound is not unrolled: its write may run, so the
// output is not "never driven".
// expect: LNT-024 WARNING 6
module top(input c, input [3:0] n,
           output logic y);
  always @(c, n) for (int i = 0; i < 2; i++) begin
    for (int j = 0; j < n; j++) y = c;
    if (i == 1) break;
  end
endmodule
