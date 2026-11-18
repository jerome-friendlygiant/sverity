// sverity-args: --top top
// Logic before and after the black box, but nothing returns to its input: no loop, whatever the box does.
// expect-none: LNT-012
(* black_box *) module macro_stage(input logic [3:0] in_i, output logic [3:0] out_o);
endmodule
module top(input logic [3:0] seed, output logic [3:0] result);
  logic [3:0] stage_in, stage_out;
  assign stage_in = ~seed;
  macro_stage u_stage(.in_i(stage_in), .out_o(stage_out));
  assign result = stage_out ^ seed;
endmodule
