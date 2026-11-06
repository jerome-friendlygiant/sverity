// Original authored reset-domain example.
module top(input logic clk_i, reset_ni, input logic data_i, output logic out_o);
 logic first_q, last_q;
 always_ff @(posedge clk_i or negedge reset_ni)
   if (!reset_ni) begin first_q <= 1'b1; last_q <= 1'b0; end
   else begin first_q <= 1'b1; last_q <= first_q; end
 always_ff @(posedge clk_i or negedge last_q)
   if (!last_q) out_o <= 0; else out_o <= data_i;
endmodule
