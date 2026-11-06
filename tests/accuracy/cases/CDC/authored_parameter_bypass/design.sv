module selectable_cdc #(
  parameter bit USE_SYNC = 1'b0
) (
  input logic dst_clk,
  input logic arst_n,
  input logic async_flag,
  output logic flag_dst
);
  generate
    if (USE_SYNC) begin : g_sync
      logic meta;
      always_ff @(posedge dst_clk or negedge arst_n) begin
        if (!arst_n) begin
          meta <= 1'b0;
          flag_dst <= 1'b0;
        end else begin
          meta <= async_flag;
          flag_dst <= meta;
        end
      end
    end else begin : g_bypass
      always_comb flag_dst = async_flag;
    end
  endgenerate
endmodule

module top #(
  parameter bit USE_SYNC = 1'b0
) (
  input logic src_clk,
  input logic dst_clk,
  input logic arst_n,
  input logic flag_src,
  output logic flag_dst
);
  selectable_cdc #(.USE_SYNC(USE_SYNC)) u_selectable (
    .dst_clk(dst_clk), .arst_n(arst_n), .async_flag(flag_src),
    .flag_dst(flag_dst)
  );
endmodule
