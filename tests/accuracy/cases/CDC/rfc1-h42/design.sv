// One-way event counter pointer compared against a local count.
module top (
  input  logic src_clk,
  input  logic dst_clk,
  input  logic dst_rst_ni,
  input  logic inc_sync_i,
  input  logic dst_inc_i,
  output logic match_o
);
  logic [3:0] bin_q;
  logic [3:0] gray_q;
  logic [3:0] s1_q;
  logic [3:0] s2_q;
  logic [3:0] decoded;
  logic [3:0] local_q;
  logic       match_q;

  always_ff @(posedge src_clk) if (inc_sync_i) bin_q <= bin_q + 4'd1;
  always_ff @(posedge src_clk) gray_q <= bin_q ^ (bin_q >> 1);

  always_ff @(posedge dst_clk) s1_q <= gray_q;
  always_ff @(posedge dst_clk) s2_q <= s1_q;

  assign decoded = {s2_q[3],
                    s2_q[3] ^ s2_q[2],
                    s2_q[3] ^ s2_q[2] ^ s2_q[1],
                    s2_q[3] ^ s2_q[2] ^ s2_q[1] ^ s2_q[0]};

  always_ff @(posedge dst_clk) begin
    if (!dst_rst_ni) local_q <= 4'd0;
    else if (dst_inc_i) local_q <= local_q + 4'd1;
  end

  always_ff @(posedge dst_clk) begin
    if (!dst_rst_ni) match_q <= 1'b0;
    else             match_q <= (decoded == local_q);
  end

  assign match_o = match_q;
endmodule
