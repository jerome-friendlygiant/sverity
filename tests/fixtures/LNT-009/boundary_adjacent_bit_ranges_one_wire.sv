// sverity-args: --top top
// Two continuous drivers on adjacent, non-overlapping bit ranges of one net: no bit has two drivers.
// expect-none: LNT-009
module top(input logic a, b, output wire [7:0] bus);
  assign bus[3:0] = {4{a}};
  assign bus[7:4] = {4{b}};
endmodule
