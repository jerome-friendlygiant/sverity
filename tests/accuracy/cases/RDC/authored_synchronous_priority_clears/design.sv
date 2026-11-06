// Original authored reset-domain example.
module top(input logic clk_i, clear_i, flush_i, input logic [7:0] data_i, output logic [7:0] out_o);
 always_ff @(posedge clk_i)
   if (clear_i) out_o <= 0;
   else if (flush_i) out_o <= 8'hff;
   else out_o <= data_i;
endmodule
