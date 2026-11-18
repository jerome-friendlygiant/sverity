// Dual-port register-file RAM with write-through: a read of the entry being
// written in the same cycle returns the incoming data.
module top (
  input  logic        clk,
  input  logic        we,
  input  logic [5:0]  waddr,
  input  logic [31:0] wdata,
  input  logic [5:0]  raddr,
  output logic [31:0] rdata
);
  logic [31:0] mem [0:63];

  always_ff @(posedge clk) begin
    if (we) mem[waddr] <= wdata;
  end

  always_ff @(posedge clk) begin
    if (we && (waddr == raddr)) rdata <= wdata;
    else                        rdata <= mem[raddr];
  end
endmodule
