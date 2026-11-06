// Original authored reset-domain example.
module top(input logic clk_i, reset_ni, save_i, restore_i, input logic [7:0] data_i, output logic [7:0] out_o);
 logic [7:0] retained_q;
 logic [1:0] ready_q;
 always_ff @(posedge clk_i) if (save_i) retained_q <= out_o;
 always_ff @(posedge clk_i or negedge reset_ni)
   if (!reset_ni) ready_q <= 0; else ready_q <= {ready_q[0],1'b1};
 always_ff @(posedge clk_i or negedge ready_q[1])
   if (!ready_q[1]) out_o <= 0;
   else if (restore_i) out_o <= retained_q;
   else out_o <= data_i;
endmodule
