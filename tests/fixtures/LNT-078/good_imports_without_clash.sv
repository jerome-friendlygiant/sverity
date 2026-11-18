// sverity-args: --top top
// A wildcard import beside local names the package does not have, and an explicit import of a name the module does not redeclare.
// expect-none: LNT-078
package p;
  localparam int Width = 8;
  localparam int Depth = 2;
endpackage
module top import p::*; (input logic [7:0] a, output logic [7:0] y);
  import p::Depth;
  localparam int Shift = 4;
  assign y = (a >> Shift) + 8'(Width) + 8'(Depth);
endmodule
