// Dual-clock buffer between a capture domain and a processing domain.
// The write port runs on clk_w, the registered read port on clk_r.
module top (
  input  logic        clk_w,
  input  logic        clk_r,
  input  logic        we,
  input  logic [6:0]  waddr,
  input  logic [15:0] wdata,
  input  logic [6:0]  raddr,
  output logic [15:0] rdata
);
  logic [15:0] mem [0:127];

  always_ff @(posedge clk_w) begin
    if (we) mem[waddr] <= wdata;
  end

  always_ff @(posedge clk_r) begin
    rdata <= mem[raddr];
  end
endmodule
