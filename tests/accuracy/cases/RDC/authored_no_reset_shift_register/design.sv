// Original authored reset-domain example.
module top(input logic clk_i, input logic [7:0] data_i, output logic [7:0] out_o);
 logic [7:0] pipe_q;
 always_ff @(posedge clk_i) begin pipe_q <= data_i; out_o <= pipe_q; end
endmodule
