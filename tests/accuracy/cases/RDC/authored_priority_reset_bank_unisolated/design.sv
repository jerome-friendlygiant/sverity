// Original authored reset-domain example.
module top(input logic clk_i, reset_a_ni, reset_b_ni, input logic [3:0] data_i, output logic [3:0] out_o);
 logic [1:0] ready_q;
 logic [3:0] bank_q;
 always_ff @(posedge clk_i or negedge reset_b_ni)
   if (!reset_b_ni) ready_q <= 0; else ready_q <= {ready_q[0],1'b1};
 always_ff @(posedge clk_i or negedge reset_a_ni)
   if (!reset_a_ni) bank_q <= 0; else bank_q <= data_i;
 always_ff @(posedge clk_i or negedge ready_q[1])
   if (!ready_q[1]) out_o <= 0; else out_o <= bank_q + 4'd1;
endmodule
