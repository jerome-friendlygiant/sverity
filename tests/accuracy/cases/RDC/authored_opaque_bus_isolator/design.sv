// Original authored reset-domain example.
(* blackbox *) module isolate(input logic clk, rst_n, source_rst_n, payload, output logic value); endmodule
module top(input logic clk_i, reset_ni, source_reset_ni, input logic data_i, output logic out_o);
 logic [1:0] ready_q;
 logic payload_q;
 wire isolated;
 always_ff @(posedge clk_i or negedge reset_ni)
   if (!reset_ni) ready_q <= 0; else ready_q <= {ready_q[0],1'b1};
 always_ff @(posedge clk_i or negedge source_reset_ni)
   if (!source_reset_ni) payload_q <= 0; else payload_q <= data_i;
 isolate u_iso(clk_i,ready_q[1],source_reset_ni,payload_q,isolated);
 always_ff @(posedge clk_i or negedge ready_q[1])
   if (!ready_q[1]) out_o <= 0; else out_o <= isolated;
endmodule
