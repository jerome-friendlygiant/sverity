// Original authored reset-domain example.
module top #(parameter int STAGES=4)(input logic clk_i, reset_ni, input logic data_i, output logic out_o);
 logic [STAGES-1:0] r_q;
 always_ff @(posedge clk_i or negedge reset_ni)
   if (!reset_ni) r_q <= '0; else r_q <= {r_q[STAGES-2:0],1'b1};
 always_ff @(posedge clk_i or negedge r_q[STAGES-1])
   if (!r_q[STAGES-1]) out_o <= 0; else out_o <= data_i;
endmodule
