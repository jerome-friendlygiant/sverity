// Original authored reset-domain example.
module top(input logic clk_i, reset_ni, input logic data_i, output logic out_o);
 logic [1:0] ready_q;
 logic delayed_reset;
 always_ff @(posedge clk_i or negedge reset_ni)
   if (!reset_ni) ready_q <= 0; else ready_q <= {ready_q[0],1'b1};
 always_ff @(posedge clk_i) delayed_reset <= reset_ni;
 always_ff @(posedge clk_i or negedge delayed_reset)
   if (!delayed_reset) out_o <= 0; else out_o <= data_i;
endmodule
