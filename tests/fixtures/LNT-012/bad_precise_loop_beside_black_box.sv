// sverity-args: --top top
// The black box feeds a real loop in logic: the loop is proven without the box, so it stays an ERROR.
// expect: LNT-012 ERROR
// expect-none: LNT-012 WARNING
(* black_box *) module macro_stage(input logic [3:0] in_i, output logic [3:0] out_o);
endmodule
module top(input logic [3:0] seed, output logic [3:0] result);
  logic [3:0] a, b;
  assign a = b ^ seed;
  assign b = a & result;
  macro_stage u_stage(.in_i(a), .out_o(result));
endmodule
