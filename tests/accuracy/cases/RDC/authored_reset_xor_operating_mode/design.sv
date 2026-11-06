// Original authored reset-domain example.
module top(input logic clk_i, reset_ni, input logic mode_i, data_i, output logic out_o);
 logic [1:0] ready_q;
 logic rst_n;
 always_ff @(posedge clk_i or negedge reset_ni)
   if (!reset_ni) ready_q <= 0; else ready_q <= {ready_q[0],1'b1};
 assign rst_n = ready_q[1] ^ mode_i;
 always_ff @(posedge clk_i or negedge rst_n)
   if (!rst_n) out_o <= 0; else out_o <= data_i;
endmodule
