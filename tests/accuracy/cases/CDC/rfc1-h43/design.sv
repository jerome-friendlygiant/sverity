// Twisted-ring sequence state observed in another clock domain.
module top (
  input  logic src_clk,
  input  logic dst_clk,
  input  logic src_rst_ni,
  input  logic dst_rst_ni,
  output logic hit_o
);
  logic [3:0] j_q;
  logic [3:0] s1_q;
  logic [3:0] s2_q;
  logic       hit_q;

  always_ff @(posedge src_clk) begin
    if (!src_rst_ni) j_q <= 4'b0000;
    else             j_q <= {j_q[2:0], ~j_q[3]};
  end

  always_ff @(posedge dst_clk) s1_q <= j_q;
  always_ff @(posedge dst_clk) s2_q <= s1_q;

  always_ff @(posedge dst_clk) begin
    if (!dst_rst_ni) hit_q <= 1'b0;
    else             hit_q <= (s2_q == 4'b0011);
  end

  assign hit_o = hit_q;
endmodule
