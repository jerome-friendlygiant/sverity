// Original authored reset-domain example.
module top(input logic clk_i, clear_i, input logic [3:0] data_i, output logic [3:0] out_o);
 logic [3:0] pipe_q [3];
 always_ff @(posedge clk_i)
   if (clear_i) begin pipe_q[0] <= 0; pipe_q[1] <= 0; pipe_q[2] <= 0; end
   else begin pipe_q[0] <= data_i; pipe_q[1] <= pipe_q[0]; pipe_q[2] <= pipe_q[1]; end
 assign out_o = pipe_q[2];
endmodule
