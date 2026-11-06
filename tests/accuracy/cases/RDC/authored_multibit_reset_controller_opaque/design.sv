// Original authored reset-domain example.
(* blackbox *) module sequencer(input logic clk, request_n, output logic [1:0] reset_n); endmodule
module top(input logic clk_i, reset_ni, input logic [1:0] data_i, output logic [1:0] out_o);
 wire [1:0] reset_vector_n;
 logic a_q;
 sequencer u_seq(clk_i,reset_ni,reset_vector_n);
 always_ff @(posedge clk_i or negedge reset_vector_n[0])
   if (!reset_vector_n[0]) a_q <= 0; else a_q <= data_i[0];
 always_ff @(posedge clk_i or negedge reset_vector_n[1])
   if (!reset_vector_n[1]) out_o <= 0; else out_o <= {data_i[1],a_q};
endmodule
