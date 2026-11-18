// sverity-args: --top top
// expect-none: LNT-057
package p;
  parameter P=8;
  typedef logic [P-1:0] t;
endpackage
module top(input logic [15:0] a);
  import p::*;
  t x;
  p::t y;
  logic [p::P-1:0] z;
  assign x=a;
  assign y=a;
  assign z=a;
endmodule
