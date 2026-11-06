// Original authored reset-domain example.
(* blackbox *) module ram_macro(input logic clk, reset_n, write_en, input logic [3:0] address, input logic [7:0] data, output logic [7:0] value); endmodule
module top(input logic clk_i, reset_ni, write_i, input logic [3:0] addr_i, input logic [7:0] data_i, output logic [7:0] out_o);
 logic [1:0] ready_q;
 always_ff @(posedge clk_i or negedge reset_ni)
   if (!reset_ni) ready_q <= 0; else ready_q <= {ready_q[0],1'b1};
 ram_macro u_ram(clk_i,ready_q[1],write_i,addr_i,data_i,out_o);
endmodule
