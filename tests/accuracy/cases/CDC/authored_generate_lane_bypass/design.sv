module lane_sync (
  input logic clk_dst,
  input logic arst_n,
  input logic d,
  output logic q
);
  logic meta;
  always_ff @(posedge clk_dst or negedge arst_n) begin
    if (!arst_n) begin
      meta <= 1'b0;
      q <= 1'b0;
    end else begin
      meta <= d;
      q <= meta;
    end
  end
endmodule

module lane_receiver #(
  parameter int LANES = 4
) (
  input logic clk_dst,
  input logic arst_n,
  input logic [LANES-1:0] req_async,
  output logic [LANES-1:0] req_seen,
  output logic any_request
);
  genvar lane;
  generate
    for (lane = 0; lane < LANES; lane++) begin : g_lane
      if (lane == 2) begin : g_special
        assign req_seen[lane] = req_async[lane];
      end else begin : g_safe
        lane_sync u_sync (
          .clk_dst(clk_dst), .arst_n(arst_n), .d(req_async[lane]),
          .q(req_seen[lane])
        );
      end
    end
  endgenerate

  always_ff @(posedge clk_dst or negedge arst_n) begin
    if (!arst_n) any_request <= 1'b0;
    else any_request <= |req_seen;
  end
endmodule

module top (
  input logic src_clk,
  input logic dst_clk,
  input logic arst_n,
  input logic [3:0] req_async,
  output logic [3:0] req_seen,
  output logic any_request
);
  lane_receiver #(.LANES(4)) u_receiver (
    .clk_dst(dst_clk), .arst_n(arst_n), .req_async(req_async),
    .req_seen(req_seen), .any_request(any_request)
  );
endmodule
