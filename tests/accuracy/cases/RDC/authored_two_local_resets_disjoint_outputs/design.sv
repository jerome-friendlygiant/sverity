// Original authored reset-domain example.
module top(input logic clk_i, reset_a_ni, reset_b_ni, input logic [1:0] data_i, output logic [1:0] out_o);
 logic [1:0] a_q,b_q;
 always_ff @(posedge clk_i or negedge reset_a_ni)
   if (!reset_a_ni) a_q <= 0; else a_q <= {a_q[0],1'b1};
 always_ff @(posedge clk_i or negedge reset_b_ni)
   if (!reset_b_ni) b_q <= 0; else b_q <= {b_q[0],1'b1};
 always_ff @(posedge clk_i or negedge a_q[1])
   if (!a_q[1]) out_o[0] <= 0; else out_o[0] <= data_i[0];
 always_ff @(posedge clk_i or negedge b_q[1])
   if (!b_q[1]) out_o[1] <= 0; else out_o[1] <= data_i[1];
endmodule
