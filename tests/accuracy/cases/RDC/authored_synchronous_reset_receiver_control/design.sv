// Original authored reset-domain example.
module top(input logic clk_i, reset_i, enable_i, input logic data_i, output logic out_o);
 logic stored_q;
 always_ff @(posedge clk_i) begin
   if (reset_i) begin stored_q <= 0; out_o <= 0; end
   else begin stored_q <= data_i; if (enable_i) out_o <= stored_q; end
 end
endmodule
