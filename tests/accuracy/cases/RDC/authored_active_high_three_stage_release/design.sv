// Original authored reset-domain example.
module top(input logic clk_i, reset_i, input logic data_i, output logic out_o);
 logic [2:0] reset_q;
 always_ff @(posedge clk_i or posedge reset_i)
   if (reset_i) reset_q <= 3'b111; else reset_q <= {reset_q[1:0],1'b0};
 always_ff @(posedge clk_i or posedge reset_q[2])
   if (reset_q[2]) out_o <= 0; else out_o <= data_i;
endmodule
