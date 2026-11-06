// Original authored reset-domain example.
module top(input logic clk_i, reset_ni, scan_reset_ni, test_mode_i, input logic data_i, output logic out_o);
 logic [1:0] ready_q;
 logic selected_n;
 always_ff @(posedge clk_i or negedge reset_ni)
   if (!reset_ni) ready_q <= 0; else ready_q <= {ready_q[0],1'b1};
 assign selected_n = test_mode_i ? scan_reset_ni : ready_q[1];
 always_ff @(posedge clk_i or negedge selected_n)
   if (!selected_n) out_o <= 0; else out_o <= data_i;
endmodule
