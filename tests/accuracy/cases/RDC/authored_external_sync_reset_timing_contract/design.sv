// Original authored reset-domain example.
module top(input logic clk_i, sync_reset_i, input logic [3:0] data_i, output logic [3:0] out_o);
 // sync_reset_i is supplied by an external controller, whose timing is unavailable.
 always_ff @(posedge clk_i)
   if (sync_reset_i) out_o <= 0; else out_o <= data_i;
endmodule
