// sverity-args: --top top
// slang unrolls a foreach exactly, so its writes count as driving every bit.
// expect-none: LNT-008
module top(input logic c, output logic [7:0] y);
  logic [7:0] v;
  always_comb foreach (v[k]) v[k] = c;
  assign y = v;
endmodule
