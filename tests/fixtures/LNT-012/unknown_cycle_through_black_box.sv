// sverity-args: --top top
// revert: assign stage_in = seed ^ result; => assign stage_in = seed;
// revert-expect-none: LNT-012
// The macro's body is not the hardware: if it passes in_i to out_o without a register, the feedback is a loop.
// expect: LNT-012 WARNING 12
(* black_box *) module macro_stage(input logic [3:0] in_i, output logic [3:0] out_o);
endmodule
module top(input logic [3:0] seed, output logic [3:0] result);
  logic [3:0] stage_in;
  assign stage_in = seed ^ result;
  // the instance below closes the loop
  macro_stage u_stage(.in_i(stage_in), .out_o(result));
endmodule
