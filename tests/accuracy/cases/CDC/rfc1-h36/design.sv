// Status flag transferred between two clock domains.
module top (
  input  logic clk_src,
  input  logic clk_dst,
  input  logic dst_rst_ni,
  input  logic flag_i,
  output logic flag_o
);
  logic src_flag_q;
  logic sync1_q;
  logic sync2_q;
  logic flag_q;

  always_ff @(posedge clk_src) src_flag_q <= flag_i;

  always_ff @(posedge clk_dst) begin
    sync1_q <= src_flag_q;
    sync2_q <= sync1_q;
  end

  always_ff @(posedge clk_dst) begin
    if (!dst_rst_ni) flag_q <= 1'b0;
    else             flag_q <= sync2_q;
  end

  assign flag_o = flag_q;
endmodule
