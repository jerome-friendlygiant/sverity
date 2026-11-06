// Original authored reset-domain example.
module top(input logic clk_i, reset_ni, input logic hold_i, data_i, output logic out_o);
 logic [1:0] ready_q;
 logic reset_n;
 always_ff @(posedge clk_i or negedge reset_ni)
   if (!reset_ni) ready_q <= 0; else ready_q <= {ready_q[0],1'b1};
 assign reset_n = ready_q[1] && !hold_i;
 always_ff @(posedge clk_i or negedge reset_n)
   if (!reset_n) out_o <= 0; else out_o <= data_i;
endmodule
