// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
(* black_box *) module memory_macro(input wire clk_i, input wire [4:0] address_i,
                                 output wire [15:0] data_o);
endmodule
module top(input wire clk_i, input wire [4:0] address_i, output wire [15:0] data_o);
  memory_macro u_mem(.clk_i(clk_i),.address_i(address_i),.data_o(data_o));
endmodule
