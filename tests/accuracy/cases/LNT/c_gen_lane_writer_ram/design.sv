// Lane-parallel store: each write lane has its own enable, address and data
// and writes into one shared 16-entry memory.
module top #(
  parameter int NUM_WR = 1
) (
  input  logic                clk,
  input  logic [NUM_WR-1:0]   we,
  input  logic [NUM_WR*4-1:0] waddr,
  input  logic [NUM_WR*8-1:0] wdata,
  input  logic [3:0]          raddr,
  output logic [7:0]          rdata
);
  logic [7:0] mem [0:15];

  generate
    for (genvar p = 0; p < NUM_WR; p++) begin : g_wr
      always @(posedge clk) begin
        if (we[p]) mem[waddr[p*4 +: 4]] <= wdata[p*8 +: 8];
      end
    end
  endgenerate

  assign rdata = mem[raddr];
endmodule
