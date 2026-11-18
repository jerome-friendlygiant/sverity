// sverity-args: --top top
// slang takes a loop with a run-time bound for never executing and lists no driver for the write in it; the write may run, and
// the second process writes the same bit, so the overlap cannot be excluded.
// expect: LNT-009 WARNING 8
module top(input logic c, input logic [1:0] n, output logic [3:0] y);
  logic [3:0] v;
  always @(c, n) for (int j = 0; j < n; j++) v[3] = c;
  always @(c) v[3] = 1'b0;
  assign y = v;
endmodule
