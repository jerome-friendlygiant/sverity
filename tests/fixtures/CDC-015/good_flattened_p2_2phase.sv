// sverity-args: --top flat_2phase --sdc @FIXTURES@/_common/async_src_dst.sdc
// Reduced flattened reproduction of common_cells cc_cdc_2phase (e73baaec).
// Provenance is recorded in the CDC family crosswalk.
// expect: CDC-015 INFO
// expect: CDC-024 WARNING
// expect-none: CDC-013 WARNING
// expect-none: CDC-014 WARNING
module flat_2phase (
  input  logic       src_rst_ni, src_clk_i,
  input  logic [7:0] src_data_i,
  input  logic       src_valid_i,
  output logic       src_ready_o,
  input  logic       dst_rst_ni, dst_clk_i,
  output logic [7:0] dst_data_o,
  output logic       dst_valid_o,
  input  logic       dst_ready_i
);
  logic req_src_q, ack_dst_q, req_synced_q1;
  logic [7:0] data_src_q, data_dst_q;
  (* async_reg = "true" *) logic [1:0] ack_sync_q;
  (* async_reg = "true" *) logic [1:0] req_sync_q;
  wire ack_synced = ack_sync_q[1];
  wire req_synced = req_sync_q[1];
  wire src_accept = src_valid_i && src_ready_o;
  assign src_ready_o = (req_src_q == ack_synced);
  always_ff @(posedge src_clk_i, negedge src_rst_ni)
    if (!src_rst_ni) ack_sync_q <= '0; else ack_sync_q <= {ack_sync_q[0], ack_dst_q};
  always_ff @(posedge src_clk_i, negedge src_rst_ni)
    if (!src_rst_ni) begin req_src_q <= 1'b0; data_src_q <= '0; end
    else if (src_accept) begin req_src_q <= ~req_src_q; data_src_q <= src_data_i; end
  wire dst_accept = dst_valid_o && dst_ready_i;
  wire dst_data_load = req_synced != req_synced_q1 && !dst_valid_o;
  assign dst_valid_o = (ack_dst_q != req_synced_q1);
  assign dst_data_o = data_dst_q;
  always_ff @(posedge dst_clk_i, negedge dst_rst_ni)
    if (!dst_rst_ni) req_sync_q <= '0; else req_sync_q <= {req_sync_q[0], req_src_q};
  always_ff @(posedge dst_clk_i, negedge dst_rst_ni)
    if (!dst_rst_ni) begin ack_dst_q <= 1'b0; req_synced_q1 <= 1'b0; data_dst_q <= '0; end
    else begin
      req_synced_q1 <= req_synced;
      if (dst_accept) ack_dst_q <= ~ack_dst_q;
      if (dst_data_load) data_dst_q <= data_src_q;
    end
endmodule
