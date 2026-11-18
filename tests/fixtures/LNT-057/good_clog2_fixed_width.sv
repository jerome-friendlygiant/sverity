// sverity-args: --top top
// `$clog2` of a literal is fixed: the truncation (if any) does not depend on a parameter.
// expect-none: LNT-057
module top (input logic [7:0] x, output logic [$clog2(8)-1:0] idx);
  assign idx = x[2:0];
endmodule
