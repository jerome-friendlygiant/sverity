// sverity-args: --top top
// expect: LNT-059 INFO 6
package defs; parameter int Width = 7; endpackage
module top(output int y);
import defs::Width;
import defs::Width;
assign y = Width;
endmodule
