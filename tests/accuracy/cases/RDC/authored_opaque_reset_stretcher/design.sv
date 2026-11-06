// Original authored reset-domain example.
(* blackbox *) module stretch(input logic clk, pulse, output logic stretched_n); endmodule
module top(input logic clk_i, reset_pulse_i, input logic data_i, output logic out_o);
 wire request_n;
 logic [1:0] ready_q;
 stretch u_stretch(clk_i,reset_pulse_i,request_n);
 always_ff @(posedge clk_i or negedge request_n)
   if (!request_n) ready_q <= 0; else ready_q <= {ready_q[0],1'b1};
 always_ff @(posedge clk_i or negedge ready_q[1])
   if (!ready_q[1]) out_o <= 0; else out_o <= data_i;
endmodule
