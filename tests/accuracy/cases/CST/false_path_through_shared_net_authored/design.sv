module status_bridge (
  input logic dst_clk_i,
  input logic src_clk_i,
  input logic rst_ni,
  input logic request_i,
  output logic status_o,
  output logic returned_o
);
  logic mode_q;
  logic dst_seen_q;
  logic sync_a_q, sync_b_q;
  logic shared_net;

  always_ff @(posedge dst_clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      mode_q <= 1'b0;
      dst_seen_q <= 1'b0;
    end else begin
      mode_q <= request_i;
      dst_seen_q <= shared_net;
    end
  end

  assign shared_net = mode_q;

  always_ff @(posedge src_clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      sync_a_q <= 1'b0;
      sync_b_q <= 1'b0;
    end else begin
      sync_a_q <= shared_net;
      sync_b_q <= sync_a_q;
    end
  end

  assign status_o = dst_seen_q;
  assign returned_o = sync_b_q;
endmodule

module top (
  input logic dst_clk_i,
  input logic src_clk_i,
  input logic rst_ni,
  input logic request_i,
  output logic status_o,
  output logic returned_o
);
  status_bridge u_bridge (.*);
endmodule
