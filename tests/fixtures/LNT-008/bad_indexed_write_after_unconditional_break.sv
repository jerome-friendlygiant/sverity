// sverity-args: --top top
// The indexed write follows an unconditional break: it never runs, so bits [3:1] of `v` have no driver.
// expect: LNT-008 WARNING 5
module top(input c, input [3:0] n, output [3:0] y);
  logic [3:0] v;
  always @(c, n) for (int i = 0; i < n; i++) begin break; v[i] = c; end
  always @(c) v[0] = 0;
  assign y = v;
endmodule
