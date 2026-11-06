// Original authored reset-domain example.
module top(input logic clk_i, reset_a_ni, reset_b_ni, input logic event_i, output logic out_o);
 logic [1:0] ready_q;
 logic pending_q;
 always_ff @(posedge clk_i or negedge reset_b_ni)
   if (!reset_b_ni) ready_q <= 0; else ready_q <= {ready_q[0],1'b1};
 always_ff @(posedge clk_i or negedge reset_a_ni)
   if (!reset_a_ni) pending_q <= 1; else pending_q <= event_i;
 always_ff @(posedge clk_i or negedge ready_q[1])
   if (!ready_q[1]) out_o <= 0; else out_o <= pending_q;
endmodule
