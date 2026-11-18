// sverity-args: --top top
// `i` starts at -2 and is compared as unsigned against 4: the first test is false, the loop runs zero times and writes nothing.
// expect-none: LNT-009
module top(input c, output logic [7:0] y);
  logic [7:0] v;
  always @(c) for (int i = -2; i < 32'd4; i++) v[3] = c;
  always @(c) v[3] = 1'b0;
  assign y = v;
endmodule
