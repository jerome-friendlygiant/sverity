// 16-bit control register with per-byte write enables.
// Interface:
//   be      byte enables, bit 0 = low byte
//   wdata   write data bus
//   rdata   register contents
//   enabled bit 0 of the register is the block enable
module top (
  input  logic        clk,
  input  logic        rst_n,
  input  logic [1:0]  be,
  input  logic [15:0] wdata,
  output logic [15:0] rdata,
  output logic        enabled
);
  logic [15:0] q;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      q <= 16'h0000;
    end else begin
      if (be[0]) q[7:0]  <= wdata[7:0];
      if (be[1]) q[15:8] <= wdata[15:8];
    end
  end

  assign rdata   = q;
  assign enabled = q[0];
endmodule
