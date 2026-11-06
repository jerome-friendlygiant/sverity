// Original authored reset-domain example.
(* blackbox *) module power_bridge(input logic clk, por_n, power_good, output logic domain_reset_n); endmodule
module top(input logic clk_i, reset_ni, power_good_i, input logic data_i, output logic out_o);
 wire domain_reset_n;
 power_bridge u_power(clk_i,reset_ni,power_good_i,domain_reset_n);
 always_ff @(posedge clk_i or negedge domain_reset_n)
   if (!domain_reset_n) out_o <= 0; else out_o <= data_i;
endmodule
