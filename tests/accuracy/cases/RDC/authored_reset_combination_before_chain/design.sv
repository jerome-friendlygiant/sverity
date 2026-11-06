// Original authored reset-domain example.
module top(input logic clk_i, por_ni, warm_ni, input logic data_i, output logic out_o);
 logic request_n;
 logic [1:0] release_q;
 assign request_n = por_ni && warm_ni;
 always_ff @(posedge clk_i or negedge request_n)
   if (!request_n) release_q <= 0; else release_q <= {release_q[0],1'b1};
 always_ff @(posedge clk_i or negedge release_q[1])
   if (!release_q[1]) out_o <= 0; else out_o <= data_i;
endmodule
