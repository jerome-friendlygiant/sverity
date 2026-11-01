// sverity-args: --top top
// a constant wider than 62 bits is outside the modelled domain: reported Unresolved, once per shared body
// expect: LNT-038 WARNING 6
// expect-count: LNT-038 WARNING 2
module child(input logic [63:0] u, output logic y);
  assign y = u == 64'hFFFF_FFFF_FFFF_FFFF;
endmodule
module top(input logic [63:0] a, b, output logic y0, y1);
  child u0(a, y0);
  child u1(b, y1);
endmodule
