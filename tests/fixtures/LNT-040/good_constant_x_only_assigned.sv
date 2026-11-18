// sverity-args: --top top
// A constant with X bits used only as an assigned value is LNT-039's finding, not a comparison.
// expect-none: LNT-040
module top(input logic a, output logic [1:0] y);
  localparam logic [1:0] DC = 2'bx1;
  always_comb y = a ? DC : 2'b00;
endmodule
