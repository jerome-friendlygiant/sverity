// sverity-args: --top top
// expect: LNT-051 INFO 5
package defs; parameter int Width = 7; endpackage
module top(input logic d, output logic y);
if (0) begin : inactive
wire [defs::Width-1:0] v;
end
assign y = d;
endmodule
// expect-none: LNT-077
