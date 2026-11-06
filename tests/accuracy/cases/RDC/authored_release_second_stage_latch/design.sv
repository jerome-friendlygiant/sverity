// Original authored reset-domain example.
module top(input logic clk_i, reset_ni, input logic data_i, output logic out_o);
 logic first_q, release_n;
 always_ff @(posedge clk_i or negedge reset_ni)
   if (!reset_ni) first_q <= 0; else first_q <= 1;
 always_latch if (clk_i) release_n = first_q;
 always_ff @(posedge clk_i or negedge release_n)
   if (!release_n) out_o <= 0; else out_o <= data_i;
endmodule
