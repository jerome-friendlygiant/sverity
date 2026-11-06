// Original authored reset-domain example.
module top(input logic clk_i, reset_a_ni, reset_b_ni, input logic data_i, output logic out_o);
 logic [1:0] ready_q;
 logic source_q, allow_q;
 always_ff @(posedge clk_i or negedge reset_b_ni)
   if (!reset_b_ni) ready_q <= 0; else ready_q <= {ready_q[0],1'b1};
 always_ff @(posedge clk_i or negedge reset_a_ni)
   if (!reset_a_ni) source_q <= 0; else source_q <= data_i;
 always_ff @(posedge clk_i or negedge ready_q[1])
   if (!ready_q[1]) begin allow_q <= 0; out_o <= 0; end
   else begin allow_q <= reset_a_ni; out_o <= allow_q && source_q; end
endmodule
