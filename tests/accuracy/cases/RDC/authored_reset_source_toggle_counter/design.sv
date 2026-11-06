// Original authored reset-domain example.
module top(input logic clk_i, reset_ni, input logic tick_i, output logic out_o);
 logic [1:0] ready_q;
 logic [3:0] count_q;
 logic block_ni;
 always_ff @(posedge clk_i or negedge reset_ni)
   if (!reset_ni) ready_q <= 0; else ready_q <= {ready_q[0],1'b1};
 always_ff @(posedge clk_i or negedge ready_q[1])
   if (!ready_q[1]) count_q <= 0; else if (tick_i) count_q <= count_q + 1'b1;
 assign block_ni = ready_q[1] && (count_q != 4'd7);
 always_ff @(posedge clk_i or negedge block_ni)
   if (!block_ni) out_o <= 0; else out_o <= !out_o;
endmodule
