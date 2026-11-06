// Original authored reset-domain example.
(* blackbox *) module reset_conditioner(input logic clock, request_n, output logic conditioned_n); endmodule
module top(input logic clk_i, reset_ni, input logic [3:0] data_i, output logic [3:0] out_o);
 wire conditioned_n;
 reset_conditioner u_reset(clk_i,reset_ni,conditioned_n);
 always_ff @(posedge clk_i or negedge conditioned_n)
   if (!conditioned_n) out_o <= 0; else out_o <= data_i;
endmodule
