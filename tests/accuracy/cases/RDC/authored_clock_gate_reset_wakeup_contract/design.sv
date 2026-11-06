// Original authored reset-domain example.
module top(input logic clk_i, reset_ni, gate_enable_i, input logic data_i, output logic out_o);
 logic gate_q;
 wire local_clk = clk_i && gate_q;
 logic [1:0] ready_q;
 always_latch if (!clk_i) gate_q = gate_enable_i;
 always_ff @(posedge local_clk or negedge reset_ni)
   if (!reset_ni) ready_q <= 0; else ready_q <= {ready_q[0],1'b1};
 always_ff @(posedge local_clk or negedge ready_q[1])
   if (!ready_q[1]) out_o <= 0; else out_o <= data_i;
endmodule
