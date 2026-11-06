// Original authored reset-domain example.
module top(input logic clk_i, reset_ni, input logic data_i, output logic out_o);
 logic [1:0] release_q;
 wire reset_high = !release_q[1];
 always_ff @(posedge clk_i or negedge reset_ni)
   if (!reset_ni) release_q <= 0; else release_q <= {release_q[0],1'b1};
 always_ff @(posedge clk_i or posedge reset_high)
   if (reset_high) out_o <= 0; else out_o <= data_i;
endmodule
