// sverity-args: --top top
// The write follows an unconditional break in its block: it never runs and builds no hardware, so it is no second driver.
// expect-none: LNT-009
module top(input c, input [3:0] n, output y);
  logic g;
  always @(c, n) for (int i = 0; i < n; i++) begin break; g = c; end
  always @(c) g = 0;
  assign y = g;
endmodule
