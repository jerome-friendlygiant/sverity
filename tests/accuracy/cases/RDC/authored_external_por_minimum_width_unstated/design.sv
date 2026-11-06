// Original authored reset-domain example.
module top(input logic clk_i, power_on_ni, input logic data_i, output logic out_o);
 logic [2:0] ready_q;
 always_ff @(posedge clk_i or negedge power_on_ni)
   if (!power_on_ni) ready_q <= 0; else ready_q <= {ready_q[1:0],1'b1};
 always_ff @(posedge clk_i or negedge ready_q[2])
   if (!ready_q[2]) out_o <= 0; else out_o <= data_i;
endmodule
