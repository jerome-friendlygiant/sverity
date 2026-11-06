// Original authored reset-domain example.
module top(input logic clk_i, reset_ni, input logic [15:0] data_i, output logic [15:0] out_o);
 logic [1:0] ready_q;
 logic [15:0] bank_q;
 always_ff @(posedge clk_i or negedge reset_ni)
   if (!reset_ni) ready_q <= 0; else ready_q <= {ready_q[0],1'b1};
 always_ff @(posedge clk_i or negedge ready_q[1])
   if (!ready_q[1]) begin bank_q <= 0; out_o <= 0; end
   else begin bank_q <= data_i; out_o <= bank_q; end
endmodule
