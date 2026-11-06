// Original authored reset-domain example.
module top(input logic clk_i, flush_i, enable_i, output logic [7:0] out_o);
 always_ff @(posedge clk_i)
   if (flush_i) out_o <= 0; else if (enable_i) out_o <= out_o + 1'b1;
endmodule
