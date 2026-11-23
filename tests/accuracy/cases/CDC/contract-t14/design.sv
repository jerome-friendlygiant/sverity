module top_cdc_rfc1_t14(input logic src_clk, dst_clk, rst_n,
 input logic gate_en,
 output logic q
);
   wire gclk=src_clk & gate_en; always_ff @(posedge gclk or negedge rst_n) if(!rst_n) q<=0; else q<=~q;
endmodule
