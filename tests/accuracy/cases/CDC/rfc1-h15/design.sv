// Dual-clock register-file buffer.
module top (
  input  logic       clk_a,
  input  logic       clk_b,
  input  logic       we_i,
  input  logic [3:0] waddr_i,
  input  logic [7:0] wdata_i,
  input  logic [3:0] raddr_i,
  output logic [7:0] rdata_o
);
  logic [7:0] mem [0:15];
  logic [7:0] rd_q;

  always_ff @(posedge clk_a) begin
    if (we_i) mem[waddr_i] <= wdata_i;
  end

  always_ff @(posedge clk_b) rd_q <= mem[raddr_i];

  assign rdata_o = rd_q;
endmodule
