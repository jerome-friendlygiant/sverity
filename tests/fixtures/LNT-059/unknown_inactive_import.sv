// sverity-args: --top top
// expect-none: LNT-059
package defs; parameter int Width = 7; endpackage
module top(input logic d, output logic y);
if (0) begin : inactive
import defs::*;
end
assign y = ~d;
endmodule
