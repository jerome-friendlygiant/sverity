// Runs a DMA beat counter in its own clock domain with a reset synchronised to that domain.
module top (
  input  logic clk_cfg,
  input  logic clk_dma,
  input  logic por_ni,
  output logic cfg_ready_o,
  output logic dma_done_o
);
  logic [1:0] cfg_rst_sync_q;
  logic [1:0] dma_rst_sync_q;
  logic [3:0] beats_q;
  logic       dma_rst_n;

  always_ff @(posedge clk_cfg or negedge por_ni)
    if (!por_ni) cfg_rst_sync_q <= 2'b00;
    else         cfg_rst_sync_q <= {cfg_rst_sync_q[0], 1'b1};

  assign cfg_ready_o = cfg_rst_sync_q[1];

  always_ff @(posedge clk_dma or negedge por_ni)
    if (!por_ni) dma_rst_sync_q <= 2'b00;
    else         dma_rst_sync_q <= {dma_rst_sync_q[0], 1'b1};

  assign dma_rst_n = dma_rst_sync_q[1];

  always_ff @(posedge clk_dma or negedge dma_rst_n)
    if (!dma_rst_n) beats_q <= '0;
    else            beats_q <= beats_q + 4'd1;

  assign dma_done_o = (beats_q == 4'hf);
endmodule
