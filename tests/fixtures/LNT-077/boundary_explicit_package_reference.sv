// sverity-args: --top top
// expect-none: LNT-077
package defs; typedef logic [3:0] nibble; endpackage
module top(input defs::nibble d, output defs::nibble q);
assign q = ~d;
endmodule
