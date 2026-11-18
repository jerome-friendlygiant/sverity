// sverity-args: --top top
// A function formal takes the name of a package parameter that the wildcard import makes visible; inside the function the formal wins silently.
// revert: int Width => int Count
// revert-expect-none: LNT-078
// expect: LNT-078 INFO 10
package p;
  localparam int Width = 8;
endpackage
module top import p::*; (input logic [7:0] a, output logic [7:0] y);
  function automatic logic [7:0] shifted(logic [7:0] v, int Width);
    return v >> Width;
  endfunction
  assign y = shifted(a, 4);
endmodule
