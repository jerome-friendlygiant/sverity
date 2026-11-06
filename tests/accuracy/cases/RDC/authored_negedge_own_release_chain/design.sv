// Original authored reset-domain example.
module top(input logic clk_i, reset_ni, input logic data_i, output logic out_o);
 logic [1:0] release_q;
 always_ff @(negedge clk_i or negedge reset_ni)
   if (!reset_ni) release_q <= 0; else release_q <= {release_q[0],1'b1};
 always_ff @(negedge clk_i or negedge release_q[1])
   if (!release_q[1]) out_o <= 0; else out_o <= data_i;
endmodule
