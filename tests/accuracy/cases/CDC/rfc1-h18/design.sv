// Software-requested reset of a clk_b block.
module top (
  input  logic clk_a,
  input  logic clk_b,
  input  logic rst_req_i,
  input  logic hold_i,
  input  logic d_i,
  output logic q_o
);
  logic rst_req_q;
  logic dst_rst_n;
  logic q_q;

  always_ff @(posedge clk_a) rst_req_q <= rst_req_i;

  assign dst_rst_n = ~(rst_req_q & ~hold_i);

  always_ff @(posedge clk_b or negedge dst_rst_n) begin
    if (!dst_rst_n) q_q <= 1'b0;
    else            q_q <= d_i;
  end

  assign q_o = q_q;
endmodule
