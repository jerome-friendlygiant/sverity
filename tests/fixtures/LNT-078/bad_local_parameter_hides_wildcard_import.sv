// sverity-args: --top top
// A local parameter takes the name of a package parameter that the wildcard import makes visible; the local one wins silently.
// revert: Width = 4 => Narrow = 4
// revert-expect-none: LNT-078
// expect: LNT-078 INFO 10
package p;
  localparam int Width = 8;
endpackage
module top import p::*; (input logic [7:0] a, output logic [7:0] y);
  localparam int Width = 4;
  assign y = a >> Width;
endmodule
