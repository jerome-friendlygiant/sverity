// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
(* black_box *) module macro_stage(input wire [7:0] in_i, output wire [7:0] out_o);
endmodule
module top(input wire [7:0] seed_i, output wire [7:0] result_o);
  wire [7:0] macro_input;
  assign macro_input = seed_i ^ result_o;
  macro_stage u_stage(.in_i(macro_input),.out_o(result_o));
endmodule
