// Line buffer with an independent write port and read port on one clock.
// The producer and the consumer each drive their own address.
module top (
  input  logic        clk,
  input  logic        we,
  input  logic [7:0]  waddr,
  input  logic [11:0] wdata,
  input  logic        re,
  input  logic [7:0]  raddr,
  output logic [11:0] rdata
);
  logic [11:0] mem [0:255];

  always_ff @(posedge clk) begin
    if (we) mem[waddr] <= wdata;
  end

  always_ff @(posedge clk) begin
    if (re) rdata <= mem[raddr];
  end
endmodule
