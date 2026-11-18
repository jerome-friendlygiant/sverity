// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT

module top #(
  parameter int unsigned Slots = 32,
  parameter int unsigned Width = 32,
  localparam int unsigned Aw   = $clog2(Slots)
) (
  input  logic             clk_i,
  input  logic             rst_ni,
  input  logic             cpu_we_i,
  input  logic [Aw-1:0]    cpu_addr_i,
  input  logic [Width-1:0] cpu_wdata_i,
  input  logic             dma_we_i,
  input  logic [Aw-1:0]    dma_addr_i,
  input  logic [Width-1:0] dma_wdata_i,
  input  logic [Aw-1:0]    rd_addr_i,
  output logic [Width-1:0] rd_data_o,
  output logic [31:0]      write_count_o
);

  logic [Width-1:0] mbox [Slots];
  logic [31:0]      write_count_q;

  always @(posedge clk_i) begin
    if (cpu_we_i) begin
      mbox[cpu_addr_i] <= cpu_wdata_i;
    end
  end

  always @(posedge clk_i) begin
    if (dma_we_i) begin
      mbox[dma_addr_i] <= dma_wdata_i;
    end
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) write_count_q <= '0;
    else         write_count_q <= write_count_q + 32'(cpu_we_i) + 32'(dma_we_i);
  end

  assign rd_data_o     = mbox[rd_addr_i];
  assign write_count_o = write_count_q;

endmodule
