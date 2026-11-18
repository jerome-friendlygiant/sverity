// Single-port 64 x 16 scratchpad. A cycle with en performs the write when we
// is set and always captures the addressed word into the read register.
module top (
  input  logic        clk,
  input  logic        en,
  input  logic        we,
  input  logic [5:0]  addr,
  input  logic [15:0] wdata,
  output logic [15:0] rdata
);
  logic [15:0] mem [0:63];

  always_ff @(posedge clk) begin
    if (en) begin
      if (we) mem[addr] <= wdata;
      rdata <= mem[addr];
    end
  end
endmodule
