// 16 x 16 register array with two write ports (CPU side and DMA side) and
// one combinational read port.
module top (
  input  logic        clk,
  input  logic        we_cpu,
  input  logic [3:0]  addr_cpu,
  input  logic [15:0] wd_cpu,
  input  logic        we_dma,
  input  logic [3:0]  addr_dma,
  input  logic [15:0] wd_dma,
  input  logic [3:0]  raddr,
  output logic [15:0] rdata
);
  logic [15:0] regs [0:15];

  always @(posedge clk) begin
    if (we_cpu) regs[addr_cpu] <= wd_cpu;
  end

  always @(posedge clk) begin
    if (we_dma) regs[addr_dma] <= wd_dma;
  end

  assign rdata = regs[raddr];
endmodule
