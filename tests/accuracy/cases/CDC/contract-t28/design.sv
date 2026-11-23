module top_cdc_rfc1_t28(input logic src_clk, dst_clk, rst_n,
 input logic div,
 output logic q, sampled
);
  logic local_clk; assign local_clk=div;
  always_ff @(posedge local_clk or negedge rst_n) if(!rst_n) q<=0; else q<=~q;
  always_ff @(posedge src_clk or negedge rst_n) if(!rst_n) sampled<=0; else sampled<=q;
endmodule
