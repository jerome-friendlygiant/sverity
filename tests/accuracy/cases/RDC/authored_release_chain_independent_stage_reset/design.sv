// Original authored reset-domain example.
module top(input logic clk_i, por_ni, debug_ni, input logic data_i, output logic out_o);
 logic first_q, last_q;
 always_ff @(posedge clk_i or negedge por_ni)
   if (!por_ni) first_q <= 0; else first_q <= 1;
 always_ff @(posedge clk_i or negedge debug_ni)
   if (!debug_ni) last_q <= 0; else last_q <= first_q;
 always_ff @(posedge clk_i or negedge last_q)
   if (!last_q) out_o <= 0; else out_o <= data_i;
endmodule
