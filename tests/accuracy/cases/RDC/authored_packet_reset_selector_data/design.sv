// Original authored reset-domain example.
module top(input logic clk_i, por_ni, input logic select_i, input logic data_i, output logic out_o);
 logic [1:0] release_q;
 logic reset_n;
 always_ff @(posedge clk_i or negedge por_ni)
   if (!por_ni) release_q <= 0; else release_q <= {release_q[0],1'b1};
 assign reset_n = select_i ? release_q[1] : 1'b1;
 always_ff @(posedge clk_i or negedge reset_n)
   if (!reset_n) out_o <= 0; else out_o <= data_i;
endmodule
