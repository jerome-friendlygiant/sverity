// Original authored reset-domain example.
module top(input logic clk_i, por_ni, warm_ni, input logic [7:0] data_i, output logic [7:0] out_o);
 logic [1:0] release_q;
 logic [7:0] payload_q;
 always_ff @(posedge clk_i or negedge por_ni)
   if (!por_ni) release_q <= 0; else release_q <= {release_q[0],1'b1};
 always_ff @(posedge clk_i or negedge warm_ni)
   if (!warm_ni) payload_q <= 0; else payload_q <= data_i;
 always_ff @(posedge clk_i or negedge release_q[1])
   if (!release_q[1]) out_o <= 0; else out_o <= warm_ni ? 8'b0 : payload_q;
endmodule
