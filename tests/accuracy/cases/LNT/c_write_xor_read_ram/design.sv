// Single-port 128 x 8 RAM. A cycle either writes or reads, never both: the
// read register only updates on cycles without a write.
module top (
  input  logic       clk,
  input  logic       en,
  input  logic       we,
  input  logic [6:0] addr,
  input  logic [7:0] wdata,
  output logic [7:0] rdata
);
  logic [7:0] mem [0:127];

  always_ff @(posedge clk) begin
    if (en) begin
      if (we) mem[addr]  <= wdata;
      else    rdata      <= mem[addr];
    end
  end
endmodule
