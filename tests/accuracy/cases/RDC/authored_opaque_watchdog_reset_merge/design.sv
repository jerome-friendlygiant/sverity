// Original authored reset-domain example.
(* blackbox *) module watchdog_reset(input logic clk, por_n, timeout, output logic request_n); endmodule
module top(input logic clk_i, reset_ni, timeout_i, input logic data_i, output logic out_o);
 wire request_n;
 logic [1:0] ready_q;
 watchdog_reset u_wdog(clk_i,reset_ni,timeout_i,request_n);
 always_ff @(posedge clk_i or negedge request_n)
   if (!request_n) ready_q <= 0; else ready_q <= {ready_q[0],1'b1};
 always_ff @(posedge clk_i or negedge ready_q[1])
   if (!ready_q[1]) out_o <= 0; else out_o <= data_i;
endmodule
