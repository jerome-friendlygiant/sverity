// Gates a peripheral reset with the status of a link that runs in its own clock domain.
module top (
  input  logic clk_core,
  input  logic clk_link,
  input  logic por_ni,
  input  logic link_up_i,
  output logic periph_rst_no,
  output logic core_active_o
);
  logic [1:0] core_rst_sync_q;
  logic       link_up_q;

  always_ff @(posedge clk_core or negedge por_ni)
    if (!por_ni) core_rst_sync_q <= 2'b00;
    else         core_rst_sync_q <= {core_rst_sync_q[0], 1'b1};

  always_ff @(posedge clk_link or negedge por_ni)
    if (!por_ni) link_up_q <= 1'b0;
    else         link_up_q <= link_up_i;

  assign periph_rst_no = core_rst_sync_q[1] & link_up_q;

  always_ff @(posedge clk_core or negedge por_ni)
    if (!por_ni) core_active_o <= 1'b0;
    else         core_active_o <= periph_rst_no;
endmodule
