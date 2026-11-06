// Original authored reset-domain example.
module top(input logic clk_i, reset_ni, input logic [3:0] data_i, output logic [3:0] out_o);
 logic [2:0] r_q;
 always_ff @(posedge clk_i or negedge reset_ni)
   if (!reset_ni) r_q <= 0; else r_q <= {r_q[1:0],1'b1};
 for (genvar k=0;k<4;k++) begin : lanes
   always_ff @(posedge clk_i or negedge r_q[2])
     if (!r_q[2]) out_o[k] <= 0; else out_o[k] <= data_i[k];
 end
endmodule
