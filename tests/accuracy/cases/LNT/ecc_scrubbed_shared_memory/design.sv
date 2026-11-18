// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT

module top #(
  parameter int unsigned Depth = 64,
  parameter int unsigned Width = 39,
  localparam int unsigned Aw   = $clog2(Depth)
) (
  input  logic             clk_i,
  input  logic             rst_ni,
  input  logic             we_i,
  input  logic [Aw-1:0]    waddr_i,
  input  logic [Width-1:0] wdata_i,
  input  logic [Aw-1:0]    raddr_i,
  output logic [Width-1:0] rdata_o,
  output logic             scrub_busy_o
);

  logic [Width-1:0] mem [Depth];

  always_ff @(posedge clk_i) begin
    if (we_i) begin
      mem[waddr_i] <= wdata_i;
    end
  end

  ecc_scrub_engine #(
    .Depth(Depth),
    .Width(Width)
  ) u_scrub (
    .clk_i  (clk_i),
    .rst_ni (rst_ni),
    .mem_io (mem),
    .busy_o (scrub_busy_o)
  );

  assign rdata_o = mem[raddr_i];

endmodule
