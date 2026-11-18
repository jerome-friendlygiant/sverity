// Two-bank tile buffer. Even pixels go to bank 0, odd pixels to bank 1;
// each bank has its own array and its own write process.
module top (
  input  logic        clk,
  input  logic        we,
  input  logic [4:0]  waddr,
  input  logic [11:0] wdata,
  input  logic [4:0]  raddr,
  output logic [11:0] rdata
);
  logic [11:0] bank0 [0:15];
  logic [11:0] bank1 [0:15];

  always_ff @(posedge clk) begin
    if (we && !waddr[0]) bank0[waddr[4:1]] <= wdata;
  end

  always_ff @(posedge clk) begin
    if (we && waddr[0]) bank1[waddr[4:1]] <= wdata;
  end

  assign rdata = raddr[0] ? bank1[raddr[4:1]] : bank0[raddr[4:1]];
endmodule
