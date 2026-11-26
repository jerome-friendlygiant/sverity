// Generates a software reset for the DMA engine from a configuration write.
module top (
  input  logic clk_cfg,
  input  logic clk_dma,
  input  logic por_ni,
  input  logic sw_rst_req_i,
  output logic dma_done_o
);
  logic       sw_rst_n_q;
  logic [3:0] beats_q;

  always_ff @(posedge clk_cfg or negedge por_ni)
    if (!por_ni) sw_rst_n_q <= 1'b1;
    else         sw_rst_n_q <= ~sw_rst_req_i;

  always_ff @(posedge clk_dma or negedge sw_rst_n_q)
    if (!sw_rst_n_q) beats_q <= '0;
    else             beats_q <= beats_q + 4'd1;

  assign dma_done_o = (beats_q == 4'hf);
endmodule
